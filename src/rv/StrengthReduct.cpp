#include "RvPasses.h"
#include <cmath>

using namespace sys;
using namespace sys::rv;

std::map<std::string, int> StrengthReduct::stats() {
  return {
    { "converted-ops", convertedTotal }
  };
}

struct Multiplier {
  int shPost;
  uint64_t mHigh;
  int l;
};

// https://gmplib.org/~tege/divcnst-pldi94.pdf
// Optimises `x / d` into multiplication.
// Refer to Figure 6.2.
Multiplier chooseMultiplier(int d) {
  constexpr int N = 32;
  // Number of bits of precision needed. Note we only need 31 bits,
  // because there's a sign bit.
  constexpr int prec = N - 1;
  
  int l = std::ceil(std::log2((double) d));
  int shPost = l;
  uint64_t mLow = (1ull << (N + l)) / d;
  uint64_t mHigh = ((1ull << (N + l)) + (1ull << (N + l - prec))) / d;
  while (mLow / 2 < mHigh / 2 && shPost > 0) {
    mLow /= 2;
    mHigh /= 2;
    shPost--;
  }
  return { shPost, mHigh, l };
}

int StrengthReduct::runImpl() {
  Builder builder;

  int converted = 0;
  
  // ===================
  // Rewrite MulOp.
  // ===================

  runRewriter([&](MulwOp *op) {
    auto x = op->getOperand(0);
    auto y = op->getOperand(1);

    // Const fold if possible.
    if (isa<LiOp>(x.defining) && isa<LiOp>(y.defining)) {
      converted++;
      auto vx = V(x.defining);
      auto vy = V(y.defining);
      builder.replace<LiOp>(op, { new IntAttr(vx * vy) });
      return true;
    }

    // Canonicalize.
    if (isa<LiOp>(x.defining) && !isa<LiOp>(y.defining)) {
      builder.replace<MulwOp>(op, { y, x });
      return true;
    }

    if (!isa<LiOp>(y.defining)) 
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
      builder.replace<SlliOp>(op, { x }, { new IntAttr(__builtin_ctz(i)) });
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
        lowerBits = builder.create<SlliwOp>({ x }, { new IntAttr(firstPlace) });

      auto upperBits = builder.create<SlliwOp>({ x }, { new IntAttr(__builtin_ctz(i - (1 << firstPlace))) });
      builder.replace<AddwOp>(op, { lowerBits, upperBits });
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
          lowerBits = builder.create<SlliwOp>({ x }, { new IntAttr(place) });

        auto upperBits = builder.create<SlliwOp>({ x }, { new IntAttr(__builtin_ctz(i + (1 << place))) });
        builder.replace<SubwOp>(op, { upperBits, lowerBits });
        return true;
      }
    }
    return false;
  });

  // ===================
  // Rewrite DivOp.
  // ===================

  runRewriter([&](DivwOp *op) {
    auto x = op->getOperand(0);
    auto y = op->getOperand(1);

    // Const fold if possible.
    if (isa<LiOp>(x.defining) && isa<LiOp>(y.defining)) {
      converted++;
      auto vx = V(x.defining);
      auto vy = V(y.defining);
      builder.replace<LiOp>(op, { new IntAttr(vx / vy) });
      return true;
    }

    if (!isa<LiOp>(y.defining))
      return false;

    auto i = V(y.defining);
    if (i == 1) {
      converted++;
      op->replaceAllUsesWith(x.defining);
      op->erase();
      return true;
    }

    if (i <= 0)
      return false;

    if (i == 2) {
      // See clang output: x / 2 should become
      //   srliw   a1, a0, 31
      //   add     a0, a0, a1
      //   sraiw   a0, a0, 1
      converted++;
      builder.setBeforeOp(op);

      Value srl = builder.create<SrliwOp>({ x }, { new IntAttr(31) });
      Value add = builder.create<AddOp>({ x, srl });
      builder.replace<SraiwOp>(op, { add }, { new IntAttr(1) });
      return true;
    }

    auto bits = __builtin_popcount(i);
    if (bits == 1) {
      // See clang output: x / 2^n should become
      //   slli    a1, a0, 1
      //   srli    a1, a1, (64 - n)
      //   add     a0, a0, a1
      //   sraiw   a0, a0, n
      auto n = __builtin_ctz(i);
      converted++;
      builder.setBeforeOp(op);

      Value ls = builder.create<SlliOp>({ x }, { new IntAttr(1) });
      Value bias = builder.create<SrliOp>({ ls }, { new IntAttr(64 - n) });
      Value plus = builder.create<AddOp>({ x, bias });
      builder.replace<SraiwOp>(op, { plus }, { new IntAttr(n) });
      return true;
    }

    // We truncate division toward zero.
    // See https://gmplib.org/~tege/divcnst-pldi94.pdf,
    // Section 5.
    // For signed integer, we know that N = 31.
    converted++;
    auto [shPost, m, l] = chooseMultiplier(i);
    auto n = x.defining;
    builder.setBeforeOp(op);
    if (m < (1ull << 31)) {
      // Issue q = SRA(MULSH(m, n), shPost) − XSIGN(n);
      // Note that this `mulsh` is for 32 bit; for 64 bit, the result is there.
      // We only need to `sra` an extra 32 bit to retrieve it.
      Value mVal = builder.create<LiOp>({ new IntAttr(m) });
      Value mulsh = builder.create<MulOp>({ n, mVal });
      Value sra = builder.create<SraiOp>({ mulsh }, { new IntAttr(32 + shPost) });
      Value xsign = builder.create<SraiOp>({ n }, { new IntAttr(31) });
      builder.replace<SubwOp>(op, { sra, xsign });
      return true;
    } else {
      // Issue q = SRA(n + MULSH(m − 2^N, n), shPost) − XSIGN(n);
      Value mVal = builder.create<LiOp>({ new IntAttr(m - (1ull << 32)) });
      Value mul = builder.create<MulOp>({ mVal, n });
      Value mulsh = builder.create<SraiOp>({ mul }, { new IntAttr(32) });
      Value add = builder.create<AddwOp>({ mulsh, n });
      Value sra = add;
      if (shPost > 0)
        sra = builder.create<SraiwOp>({ add }, { new IntAttr(shPost) });
      
      Value xsign = builder.create<SraiwOp>({ n }, { new IntAttr(31) });
      builder.replace<SubwOp>(op, { sra, xsign });
      return true;
    }

    return false;
  });

  // ===================
  // Rewrite ModOp.
  // ===================

  runRewriter([&](RemwOp *op) {
    auto x = op->getOperand(0);
    auto y = op->getOperand(1);

    // Const fold if possible.
    if (isa<LiOp>(x.defining) && isa<LiOp>(y.defining)) {
      auto vx = V(x.defining);
      auto vy = V(y.defining);
      builder.replace<LiOp>(op, { new IntAttr(vx % vy) });
      return true;
    }

    if (!isa<LiOp>(y.defining))
      return false;

    int i = V(y.defining);

    if (i < 0) {
      // x % i == x % -i always holds.
      V(y.defining) = -i;
      return true;
    }

    if (i == 2) {
      // Clang output of x % 2:
      //   srliw   a1, a0, 31
      //   add     a1, a1, a0
      //   andi    a1, a1, -2
      //   subw    a0, a0, a1
      converted++;
      builder.setBeforeOp(op);

      Value srl = builder.create<SrliwOp>({ x }, { new IntAttr(31) });
      Value plus = builder.create<AddOp>({ x, srl });
      Value andi = builder.create<AndiOp>({ plus }, { new IntAttr(-2) });
      builder.replace<SubwOp>(op, { x, andi });
      return true;
    }

    if (__builtin_popcount(i) == 1) {
      // Clang output of x % 2^n:
      //   slli    a1, a0, 1
      //   srli    a1, a1, (64 - n)
      //   add     a1, a1, a0
      //   andi    a1, a1, -2^n
      //   subw    a0, a0, a1
      converted++;
      builder.setBeforeOp(op);
      
      int n = __builtin_ctz(i);
      Value ls = builder.create<SlliOp>({ x }, { new IntAttr(1) });
      Value bias = builder.create<SrliOp>({ ls }, { new IntAttr(64 - n) });
      Value plus = builder.create<AddOp>({ x, bias });
      Value andi;
      if (i <= 2048)
        andi = builder.create<AndiOp>({ plus }, { new IntAttr(-i) });
      else {
        Value value = builder.create<LiOp>({ new IntAttr(-i) });
        andi = builder.create<AndOp>({ plus, value });
      }
      builder.replace<SubwOp>(op, { x, andi });
      return true;
    }

    // Replace with div-mul-sub.
    //   x % y
    // becomes
    //   %quot = x / y
    //   %mul = %quot * y
    //   x - %mul
    converted++;
    builder.setBeforeOp(op);
    auto quot = builder.create<DivwOp>(op->getOperands(), op->getAttrs());
    auto mul = builder.create<MulwOp>({ quot, y });
    builder.replace<SubwOp>(op, { x, mul });

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
}
