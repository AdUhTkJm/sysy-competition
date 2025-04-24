#include "RvPasses.h"
#include "RvOps.h"
#include "../codegen/CodeGen.h"

using namespace sys::rv;

void Lower::run() {
  Builder builder;

  runRewriter([&](IntOp *op) {
    builder.replace<MvOp>(op, op->getAttrs());
    return true;
  });

  runRewriter([&](AddIOp *op) {
    builder.replace<AddOp>(op, op->getOperands(), op->getAttrs());
    return true;
  });

  runRewriter([&](SubIOp *op) {
    builder.replace<SubOp>(op, op->getOperands(), op->getAttrs());
    return true;
  });

  runRewriter([&](MulIOp *op) {
    builder.replace<MulOp>(op, op->getOperands(), op->getAttrs());
    return true;
  });

  runRewriter([&](DivIOp *op) {
    builder.replace<DivOp>(op, op->getOperands(), op->getAttrs());
    return true;
  });

  runRewriter([&](ModIOp *op) {
    auto denom = op->getOperand(0);
    auto nom = op->getOperand(1);

    builder.setBeforeOp(op);
    auto quot = builder.create<DivOp>(op->getOperands(), op->getAttrs());
    auto mul = builder.create<MulOp>({ quot, nom });
    builder.replace<SubOp>(op, { denom, mul });
    return true;
  });

  runRewriter([&](BranchOp *op) {
    auto cond = op->getOperand().defining;

    // Only merge when the value of `cond` is not used elsewhere.
    if (isa<EqOp>(cond) && cond->getUses().size() == 1) {
      builder.replace<BeqOp>(op, cond->getOperands(), op->getAttrs());
      cond->erase();
      return true;
    }

    if (isa<NeOp>(cond) && cond->getUses().size() == 1) {
      builder.replace<BneOp>(op, cond->getOperands(), op->getAttrs());
      cond->erase();
      return true;
    }

    builder.replace<BnezOp>(op, op->getOperands(), op->getAttrs());
    return true;
  });

  runRewriter([&](GotoOp *op) {
    builder.replace<JOp>(op, op->getAttrs());
    return true;
  });
  
}
