#include "Passes.h"

using namespace sys;

std::map<std::string, int> StrengthReduct::stats() {
  return {
    { "converted-ops", converted }
  };
}

void StrengthReduct::run() {
  Builder builder;

  runRewriter([&](MulIOp *op) {
    auto x = op->getOperand(0);
    auto y = op->getOperand(1);

    // Canonicalize.
    if (isa<IntOp>(x.defining)) {
      builder.replace<MulIOp>(op, { y, x });
      return true;
    }

    if (!isa<IntOp>(y.defining)) 
      return false;

    auto i = y.defining->getAttr<IntAttr>()->value;
    if (i == 1) {
      converted++;
      op->replaceAllUsesWith(x.defining);
      return true;
    }

    auto bits = __builtin_popcount(i);

    if (bits == 1) {
      converted++;
      builder.replace<LShiftImmOp>(op, { x }, {
        new IntAttr(__builtin_ctz(i))
      });
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
        lowerBits = builder.create<LShiftImmOp>({ x }, {
          new IntAttr(firstPlace)
        });

      auto upperBits = builder.create<LShiftImmOp>({ x }, {
        new IntAttr(__builtin_ctz(i - (1 << firstPlace)))
      });
      builder.replace<AddIOp>(op, { lowerBits, upperBits });
      return true;
    }

    // Similar to above, but for sub instead of add.
    for (int place = 0; place < 31; place++) {
      if (__builtin_popcount(i + (1 << place)) == 1) {
        converted++;
        Op *lowerBits;
        if (place == 0) // Multiplying by 1
          lowerBits = x.defining;
        else
          lowerBits = builder.create<LShiftImmOp>({ x }, {
            new IntAttr(place)
          });

        auto upperBits = builder.create<LShiftImmOp>({ x }, {
          new IntAttr(__builtin_ctz(i + (1 << place)))
        });
        builder.replace<SubIOp>(op, { upperBits, lowerBits });
        return true;
      }
    }
    return false;
  });

  runRewriter([&](DivIOp *op) {
    auto x = op->getOperand(0);
    auto y = op->getOperand(1);

    // Canonicalize.
    if (isa<IntOp>(x.defining)) {
      builder.replace<DivIOp>(op, { y, x });
      return true;
    }

    if (!isa<IntOp>(y.defining))
      return false;

    auto i = y.defining->getAttr<IntAttr>()->value;
    if (i == 1) {
      converted++;
      op->replaceAllUsesWith(x.defining);
      return true;
    }

    auto bits = __builtin_popcount(i);
    if (bits == 1) {
      auto place = __builtin_ctz(i);
      converted++;
      builder.replace<RShiftImmOp>(op, { x }, {
        new IntAttr(place)
      });
      return true;
    }

    return false;
  });
}
