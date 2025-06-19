#include "ArmPasses.h"

using namespace sys::arm;
using namespace sys;

// See rv/StrengthReduct.cpp
struct Multiplier;
Multiplier chooseMultiplier(int d);

std::map<std::string, int> StrengthReduct::stats() {
  return {
    { "converted-ops", convertedTotal }
  };
}

void StrengthReduct::run() {
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

  convertedTotal += converted;
}
