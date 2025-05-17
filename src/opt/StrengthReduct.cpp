#include "Passes.h"
#include <cmath>
#include <cstdint>

using namespace sys;

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

  runRewriter([&](MulIOp *op) {
    auto x = op->getOperand(0);
    auto y = op->getOperand(1);

    // Const fold if possible.
    if (isa<IntOp>(x.defining) && isa<IntOp>(y.defining)) {
      converted++;
      auto vx = V(x.defining);
      auto vy = V(y.defining);
      builder.replace<IntOp>(op, { new IntAttr(vx * vy) });
      return true;
    }

    // Canonicalize.
    if (isa<IntOp>(x.defining) && !isa<IntOp>(y.defining)) {
      builder.replace<MulIOp>(op, { y, x });
      return true;
    }

    if (!isa<IntOp>(y.defining)) 
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
      auto amt = builder.create<IntOp>({ new IntAttr(__builtin_ctz(i)) });
      builder.replace<LShiftOp>(op, { x, amt });
      return true;
    }

    if (bits == 2) {
      converted++;
      builder.setBeforeOp(op);
      int firstPlace = __builtin_ctz(i);
      Op *lowerBits;
      if (firstPlace == 0) // Multiplying by 1
        lowerBits = x.defining;
      else {
        auto amt = builder.create<IntOp>({ new IntAttr(firstPlace) });
        lowerBits = builder.create<LShiftOp>({ x, amt });
      }

      auto amt = builder.create<IntOp>({ new IntAttr(__builtin_ctz(i - (1 << firstPlace))) });
      auto upperBits = builder.create<LShiftOp>({ x, amt });
      builder.replace<AddIOp>(op, { lowerBits, upperBits });
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
        else {
          auto amt = builder.create<IntOp>({ new IntAttr(place) });
          lowerBits = builder.create<LShiftOp>({ x, amt });
        }

        auto amt = builder.create<IntOp>({ new IntAttr(__builtin_ctz(i + (1 << place))) });
        auto upperBits = builder.create<LShiftOp>({ x, amt });
        builder.replace<SubIOp>(op, { upperBits, lowerBits });
        return true;
      }
    }
    return false;
  });

  // ===================
  // Rewrite DivOp.
  // ===================

  runRewriter([&](DivIOp *op) {
    auto x = op->getOperand(0);
    auto y = op->getOperand(1);

    // Const fold if possible.
    if (isa<IntOp>(x.defining) && isa<IntOp>(y.defining)) {
      converted++;
      auto vx = V(x.defining);
      auto vy = V(y.defining);
      builder.replace<IntOp>(op, { new IntAttr(vx / vy) });
      return true;
    }

    if (!isa<IntOp>(y.defining))
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

    auto bits = __builtin_popcount(i);
    if (bits == 1) {
      auto place = __builtin_ctz(i);
      converted++;
      builder.setBeforeOp(op);
      Value amt = builder.create<IntOp>({ new IntAttr(place) });
      builder.replace<RShiftOp>(op, { x, amt });
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
      Value mVal = builder.create<IntOp>({ new IntAttr(m) });
      Value mulsh = builder.create<MulLOp>({ mVal, n });
      Value amt = builder.create<IntOp>({ new IntAttr(32 + shPost) });
      Value sra = builder.create<RShiftLOp>({ mulsh, amt });
      Value amt2 = builder.create<IntOp>({ new IntAttr(31) });
      Value xsign = builder.create<RShiftOp>({ n, amt2 });
      builder.replace<SubIOp>(op, { sra, xsign });
      return true;
    } else {
      // Issue q = SRA(n + MULSH(m − 2^N, n), shPost) − XSIGN(n);
      Value mVal = builder.create<IntOp>({ new IntAttr(m - (1ull << 32)) });
      Value mul = builder.create<MulLOp>({ mVal, n });
      Value amt = builder.create<IntOp>({ new IntAttr(32) });
      Value mulsh = builder.create<RShiftLOp>({ mul, amt });
      Value add = builder.create<AddIOp>({ mulsh, n });
      Value sra = add;
      if (shPost > 0) {
        Value amt = builder.create<IntOp>({ new IntAttr(shPost) });
        sra = builder.create<RShiftOp>({ add, amt });
      }
      Value amt2 = builder.create<IntOp>({ new IntAttr(31) });
      Value xsign = builder.create<RShiftOp>({ n, amt2 });
      builder.replace<SubIOp>(op, { sra, xsign });
      return true;
    }

    return false;
  });

  // ===================
  // Rewrite ModOp.
  // ===================

  runRewriter([&](ModIOp *op) {
    auto x = op->getOperand(0);
    auto y = op->getOperand(1);

    // Const fold if possible.
    if (isa<IntOp>(x.defining) && isa<IntOp>(y.defining)) {
      auto vx = V(x.defining);
      auto vy = V(y.defining);
      builder.replace<IntOp>(op, { new IntAttr(vx % vy) });
      return true;
    }

    if (!isa<IntOp>(y.defining))
      return false;

    int i = V(y.defining);

    if (i <= 0)
      return false;

    //   x % (1 << n)
    // becomes
    //   x & ((1 << n) - 1)
    if (__builtin_popcount(i) == 1) {
      converted++;
      builder.setBeforeOp(op);
      auto value = builder.create<IntOp>({ new IntAttr(i - 1) });
      builder.replace<AndIOp>(op, { x, value });
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
    auto quot = builder.create<DivIOp>(op->getOperands(), op->getAttrs());
    auto mul = builder.create<MulIOp>({ quot, y });
    builder.replace<SubIOp>(op, { x, mul });

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
