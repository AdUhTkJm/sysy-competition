#include "RvPasses.h"
#include "RvOps.h"

#include "../codegen/Attrs.h"
#include "../codegen/CodeGen.h"

using namespace sys::rv;
using namespace sys;

std::map<std::string, int> InstCombine::stats() {
  return {
    { "combined-instructions", combined }
  };
}

bool liInRange(Op *op) {
  auto attr = op->getAttr<IntAttr>();
  return attr->value >= -2048 && attr->value <= 2047;
}

void InstCombine::run() {
  Builder builder;

  runRewriter([&](AddOp *op) {
    auto x = op->getOperand(0).defining;
    auto y = op->getOperand(1).defining;
    if (isa<LiOp>(x) && liInRange(x)) {
      combined++;
      builder.replace<AddiwOp>(op, { y }, { x->getAttr<IntAttr>() });
      return true;
    }

    if (isa<LiOp>(y) && liInRange(y)) {
      combined++;
      builder.replace<AddiwOp>(op, { x }, { y->getAttr<IntAttr>() });
      return true;
    }

    return false;
  });

  runRewriter([&](MulwOp *op) {
    auto x = op->getOperand(0);
    auto y = op->getOperand(1);

    // Canonicalize.
    if (isa<LiOp>(x.defining)) {
      builder.replace<MulwOp>(op, { y, x });
      return true;
    }

    if (!isa<LiOp>(y.defining)) 
      return false;

    auto i = y.defining->getAttr<IntAttr>()->value;
    if (i == 1) {
      combined++;
      op->replaceAllUsesWith(x.defining);
      return true;
    }

    auto bits = __builtin_popcount(i);

    if (bits == 1) {
      combined++;
      builder.replace<SlliwOp>(op, { x }, {
        new IntAttr(__builtin_ctz(i))
      });
      return true;
    }

    if (bits == 2) {
      combined++;
      builder.setBeforeOp(op);
      int firstPlace = __builtin_ctz(i);
      Op *lowerBits;
      if (firstPlace == 0) // Multiplying by 1
        lowerBits = x.defining;
      else
        lowerBits = builder.create<SlliwOp>({ x }, {
          new IntAttr(firstPlace)
        });

      auto upperBits = builder.create<SlliwOp>({ x }, {
        new IntAttr(__builtin_ctz(i - (1 << firstPlace)))
      });
      builder.replace<AddOp>(op, { lowerBits, upperBits });
      return true;
    }

    // Similar to above, but for sub instead of add.
    for (int place = 0; place < 31; place++) {
      if (__builtin_popcount(i + (1 << place)) == 1) {
        combined++;
        Op *lowerBits;
        if (place == 0) // Multiplying by 1
          lowerBits = x.defining;
        else
          lowerBits = builder.create<SlliwOp>({ x }, {
            new IntAttr(place)
          });

        auto upperBits = builder.create<SlliwOp>({ x }, {
          new IntAttr(__builtin_ctz(i + (1 << place)))
        });
        builder.replace<SubOp>(op, { upperBits, lowerBits });
        return true;
      }
    }
    return false;
  });
}
