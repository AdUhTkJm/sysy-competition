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

bool inRange(Op *op) {
  auto attr = op->getAttr<IntAttr>();
  return attr->value >= -2048 && attr->value <= 2047;
}

int inRange(int x) {
  return x >= -2048 && x <= 2047;
}

void InstCombine::run() {
  Builder builder;

  runRewriter([&](AddOp *op) {
    auto x = op->getOperand(0).defining;
    auto y = op->getOperand(1).defining;
    if (isa<LiOp>(x) && inRange(x)) {
      combined++;
      builder.replace<AddiOp>(op, { y }, { x->getAttr<IntAttr>() });
      return true;
    }

    if (isa<LiOp>(y) && inRange(y)) {
      combined++;
      builder.replace<AddiOp>(op, { x }, { y->getAttr<IntAttr>() });
      return true;
    }

    return false;
  });

  runRewriter([&](AddwOp *op) {
    auto x = op->getOperand(0).defining;
    auto y = op->getOperand(1).defining;
    if (isa<LiOp>(x) && inRange(x)) {
      combined++;
      builder.replace<AddiwOp>(op, { y }, { x->getAttr<IntAttr>() });
      return true;
    }

    if (isa<LiOp>(y) && inRange(y)) {
      combined++;
      builder.replace<AddiwOp>(op, { x }, { y->getAttr<IntAttr>() });
      return true;
    }

    return false;
  });

  runRewriter([&](StoreOp *op) {
    auto value = op->getOperand(0);
    auto addr = op->getOperand(1).defining;
    if (isa<AddiOp>(addr)) {
      auto offset = addr->getAttr<IntAttr>()->value;
      auto &currentOffset = op->getAttr<IntAttr>()->value;
      if (inRange(offset + currentOffset)) {
        currentOffset += offset;
        auto base = addr->getOperand();
        builder.replace<StoreOp>(op, { value, base }, op->getAttrs());
        return true;
      }
    }
    return false;
  });

  runRewriter([&](AddiwOp *op) {
    if (op->getAttr<IntAttr>()->value == 0) {
      op->replaceAllUsesWith(op->getOperand().defining);
      op->erase();
      return true;
    }
    return false;
  });

  runRewriter([&](AddiOp *op) {
    if (op->getAttr<IntAttr>()->value == 0) {
      op->replaceAllUsesWith(op->getOperand().defining);
      op->erase();
      return true;
    }
    return false;
  });
}
