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
}
