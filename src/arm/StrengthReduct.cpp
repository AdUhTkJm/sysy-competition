#include "ArmPasses.h"
#include "../opt/LoopPasses.h"

using namespace sys::arm;
using namespace sys;

// See rv/StrengthReduct.cpp
struct Multiplier {
  int shPost;
  uint64_t mHigh;
  int l;
};

Multiplier chooseMultiplier(int d);

namespace {

void hoistMovi(LoopInfo *loop) {
  for (auto subloop : loop->subloops)
    hoistMovi(subloop);

  if (!loop->preheader)
    return;
  auto term = loop->preheader->getLastOp();

  for (auto bb : loop->bbs) {
    auto ops = bb->getOps();
    for (auto op : ops) {
      if (isa<MovIOp>(op))
        op->moveBefore(term);
    }
  }
}

}

std::map<std::string, int> StrengthReduct::stats() {
  return {
    { "converted-ops", convertedTotal }
  };
}

int StrengthReduct::runImpl() {
  int converted = 0;
  Builder builder;

  runRewriter([&](MulWOp *op) {
    auto x = op->getOperand(0);
    auto y = op->getOperand(1);

    // Const fold if possible.
    if (isa<MovIOp>(x.defining) && isa<MovIOp>(y.defining)) {
      converted++;
      auto vx = V(x.defining);
      auto vy = V(y.defining);
      builder.replace<MovIOp>(op, { new IntAttr(vx * vy) });
      return true;
    }

    // Canonicalize.
    if (isa<MovIOp>(x.defining) && !isa<MovIOp>(y.defining)) {
      builder.replace<MulWOp>(op, { y, x });
      return true;
    }

    if (!isa<MovIOp>(y.defining)) 
      return false;

    auto i = V(y.defining);
    if (i < 0)
      return false;

    if (i == 1) {
      converted++;
      op->replaceAllUsesWith(x.defining);
      op->erase();
      return true;
    }

    auto bits = __builtin_popcount(i);

    if (bits == 1) {
      converted++;
      builder.setBeforeOp(op);
      builder.replace<LslXIOp>(op, { x }, { new IntAttr(__builtin_ctz(i)) });
      return true;
    }

    if (bits == 2) {
      converted++;
      builder.setBeforeOp(op);
      int firstPlace = __builtin_ctz(i);
      Op *lowerBits;
      if (firstPlace == 0) // Multiplying by 1
        lowerBits = x.defining;
      else
        lowerBits = builder.create<LslWIOp>({ x }, { new IntAttr(firstPlace) });

      auto upperBits = builder.create<LslWIOp>({ x }, { new IntAttr(__builtin_ctz(i - (1 << firstPlace))) });
      builder.replace<AddWOp>(op, { lowerBits, upperBits });
      return true;
    }

    // Similar to above, but for sub instead of add.
    for (int place = 0; place < 31; place++) {
      if (__builtin_popcount(i + (1 << place)) == 1) {
        converted++;
        builder.setBeforeOp(op);
        Op *lowerBits;
        if (place == 0) // Multiplying by 1
          lowerBits = x.defining;
        else
          lowerBits = builder.create<LslWIOp>({ x }, { new IntAttr(place) });

        auto upperBits = builder.create<LslWIOp>({ x }, { new IntAttr(__builtin_ctz(i + (1 << place))) });
        builder.replace<SubWOp>(op, { upperBits, lowerBits });
        return true;
      }
    }
    return false;
  });

  // Modulus. It's lowered to sdiv + msub, so do it before sdiv is lowered.
  runRewriter([&](MsubWOp *op) {
    // x % y is lowered as (msubw (sdivw x y) y x). Match this pattern.
    auto z = op->DEF(0);
    auto y = op->DEF(1);
    auto x = op->DEF(2);
    
    if (!isa<SdivWOp>(z) || z->DEF(0) != x || z->DEF(1) != y) {
      return false;
    }

    // A mod is detected. Now time to make it work.
    if (isa<MovIOp>(x) && isa<MovIOp>(y)) {
      converted++;
      builder.replace<MovIOp>(op, { new IntAttr(V(x) % V(y)) });
      return true;
    }

    if (!isa<MovIOp>(y))
      return false;

    auto i = V(y);
    if (i < 0)
      return false;

    if (i == 2) {
      converted++;
      builder.setBeforeOp(op);

      // and     w8, w0, #1
      // cmp     w0, #0
      // cneg    w0, w8, lt
      Value _and = builder.create<AndIOp>({ x }, { new IntAttr(1) });
      builder.replace<CnegLtZOp>(op, { x, _and });
      return true;
    }

    return false;
  });

  runRewriter([&](SdivWOp *op) {
    auto x = op->DEF(0);
    auto y = op->DEF(1);

    if (isa<MovIOp>(x) && isa<MovIOp>(y)) {
      converted++;
      builder.replace<MovIOp>(op, { new IntAttr(V(x) / V(y)) });
      return true;
    }

    if (!isa<MovIOp>(y))
      return false;

    auto i = V(y);
    if (i < 0)
      return false;

    if (i == 1) {
      converted++;
      op->replaceAllUsesWith(x);
      op->erase();
      return true;
    }

    if (i == 2) {
      converted++;
      builder.setBeforeOp(op);

      // add     w8, w0, w0, lsr #31
      // asr     w0, w8, #1
      Value add = builder.create<AddWROp>({ x, x }, { new IntAttr(31) });
      builder.replace<AsrWIOp>(op, { add }, { new IntAttr(1) });
      return true;
    }

    if (__builtin_popcount(i) == 1) {
      converted++;
      builder.setBeforeOp(op);

      // add     w8, w0, #(2^n - 1)
      // cmp     w0, #0
      // csel    w8, w8, w0, lt
      // asr     w0, w8, #n
      Value vi = builder.create<MovIOp>({ new IntAttr(i - 1) });
      Value add = builder.create<AddWOp>({ x, vi });
      Value csel = builder.create<CselLtZOp>({ x, add, x });
      builder.replace<AsrWIOp>(op, { csel }, { new IntAttr(__builtin_ctz(i)) });
      return true;
    }

    // Similar to the thing at RISC-V.
    converted++;
    auto [shPost, m, l] = chooseMultiplier(i);
    builder.setBeforeOp(op);
    if (m < (1ull << 31)) {
      Value mVal = builder.create<MovIOp>({ new IntAttr(m) });
      Value mulsh = builder.create<SmullOp>({ x, mVal });
      Value sra = builder.create<AsrXIOp>({ mulsh }, { new IntAttr(32 + shPost) });
      builder.replace<AddWROp>(op, { sra, sra }, { new IntAttr(31) });
      return true;
    } else {
      Value mVal = builder.create<MovIOp>({ new IntAttr(m - (1ull << 32)) });
      Value mul = builder.create<SmullOp>({ mVal, x });
      Value mulsh = builder.create<AsrXIOp>({ mul }, { new IntAttr(32) });
      Value add = builder.create<AddWOp>({ mulsh, x });
      Value sra = add;
      if (shPost > 0)
        sra = builder.create<AsrWIOp>({ add }, { new IntAttr(shPost) });
      
      Value xsign = builder.create<AsrWIOp>({ x }, { new IntAttr(31) });
      builder.replace<SubWOp>(op, { sra, xsign });
      return true;
    }

    return false;
  });

  runRewriter([&](SdivXOp *op) {
    auto x = op->DEF(0);
    auto y = op->DEF(1);

    // Currently, DivOp can only be emitted by SCEV.
    // It will be of a pattern (x / (1 << n)),
    // which can be fold according to `DivwOp` above.
    // We check this pattern here.
    if (isa<LslXOp>(y) && isa<MovIOp>(y->DEF(0)) && V(y->DEF(0)) == 1) {
      converted++;
      builder.setBeforeOp(op);

      // I believe `cmp + csel` is not as good as asr + lsl.
      //   srai    a1, a0, 63
      //   srl     a1, a1, (64 - n)
      //   add     a0, a0, a1
      //   sra     a0, a0, n

      auto n = y->DEF(1);
      auto srai = builder.create<AsrXIOp>({ x }, { new IntAttr(63) });
      auto vi = builder.create<MovIOp>({ new IntAttr(64) });
      auto sub = builder.create<SubWOp>({ vi, n });
      auto srl = builder.create<LsrXOp>({ srai, sub });
      auto add = builder.create<AddXOp>({ x, srl });
      builder.replace<AsrXOp>(op, { add, n });
      return true;
    }

    return false;
  });

  return converted;
}

void StrengthReduct::run() {
  int converted;
  do {
    converted = runImpl();
    convertedTotal += converted;
  } while (converted);

  // After conversion, we might produce more `mov`s.
  // We can pull them out of loop.
  LoopAnalysis analysis(module);
  analysis.run();
  auto forests = analysis.getResult();
  for (const auto &[func, forest] : forests) {
    for (auto loop : forest.getLoops()) {
      if (!loop->getParent())
        hoistMovi(loop);
    }
  }
}