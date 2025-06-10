#include "ArmPasses.h"
#include <cmath>

using namespace sys;
using namespace sys::arm;

// NOTE: A copy-paste from RV backend with some slight altering.

std::map<std::string, int> StrengthReduct::stats() {
  return {
    { "converted-ops", convertedTotal }
  };
}

namespace {

struct Multiplier {
  int shPost;
  uint64_t mHigh;
  int l;
};

}

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
      builder.replace<LslWIOp>(op, { x }, { new IntAttr(__builtin_ctz(i)) });
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

  // ===================
  // Rewrite DivOp.
  // ===================

  runRewriter([&](SdivWOp *op) {
    auto x = op->getOperand(0);
    auto y = op->getOperand(1);

    // Const fold if possible.
    if (isa<MovIOp>(x.defining) && isa<MovIOp>(y.defining)) {
      converted++;
      auto vx = V(x.defining);
      auto vy = V(y.defining);
      builder.replace<MovIOp>(op, { new IntAttr(vx / vy) });
      return true;
    }

    if (!isa<MovIOp>(y.defining))
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
      //  add     w8, w0, w0, lsr #31
      //  asr     w0, w8, #1
      converted++;
      builder.setBeforeOp(op);

      Value add = builder.create<AddWROp>({ x, x }, { new IntAttr(31) });
      builder.replace<AsrWIOp>(op, { add }, { new IntAttr(1) });
      return true;
    }

    auto bits = __builtin_popcount(i);
    if (bits == 1) {
      // See clang output: x / 2^n should become
      // add     w8, w0, #(2^n - 1)
      // cmp     w0, #0
      // csel    w8, w8, w0, lt
      // asr     w0, w8, #n
      auto n = __builtin_ctz(i);
      converted++;
      builder.setBeforeOp(op);

      Value add;
      if (i > 2048) {
        Value vi = builder.create<MovIOp>({ new IntAttr(i - 1) });
        add = builder.create<AddWOp>({ x, vi });
      } else {
        add = builder.create<AddWIOp>({ x }, { new IntAttr(i - 1) });
      }
      Value bias = builder.create<CselLtZOp>({ x, add, x });
      builder.replace<AsrWIOp>(op, { bias }, { new IntAttr(n) });
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
}
