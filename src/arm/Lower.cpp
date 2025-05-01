#include "ArmPasses.h"
#include "ArmOps.h"
#include "../codegen/CodeGen.h"

using namespace sys::arm;

void Lower::run() {
  Builder builder;

  runRewriter([&](IntOp *op) {
    builder.replace<MovOp>(op, op->gets());
    return true;
  });

  runRewriter([&](AddIOp *op) {
    builder.replace<AddOp>(op, op->getOperands(), op->gets());
    return true;
  });

  runRewriter([&](SubIOp *op) {
    builder.replace<SubOp>(op, op->getOperands(), op->gets());
    return true;
  });

  runRewriter([&](MulIOp *op) {
    builder.replace<MulOp>(op, op->getOperands(), op->gets());
    return true;
  });

  runRewriter([&](DivIOp *op) {
    builder.replace<SdivOp>(op, op->getOperands(), op->gets());
    return true;
  });

  runRewriter([&](ModIOp *op) {
    auto denom = op->getOperand(0);
    auto nom = op->getOperand(1);

    builder.setBeforeOp(op);
    auto quot = builder.create<SdivOp>(op->getOperands(), op->gets());
    builder.replace<MlaOp>(op, { quot, nom, denom });
    return true;
  });
  
}
