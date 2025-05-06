#include "ArmPasses.h"

using namespace sys::arm;

#define REPLACE(BeforeTy, AfterTy) \
  runRewriter([&](BeforeTy *op) { \
    builder.replace<AfterTy>(op, op->getOperands(), op->getAttrs()); \
    return true; \
  });

void Lower::run() {
  Builder builder;

  REPLACE(IntOp, MovOp);
  REPLACE(AddIOp, AddWOp);
  REPLACE(AddLOp, AddXOp);
  REPLACE(SubIOp, SubWOp);
  REPLACE(SubLOp, SubXOp);
  REPLACE(MulIOp, MulWOp);
  REPLACE(MulLOp, MulXOp);
  REPLACE(MulshOp, SmulhOp);
  REPLACE(MuluhOp, UmulhOp);
  REPLACE(DivIOp, SdivWOp);
  REPLACE(DivLOp, SdivXOp);
  REPLACE(LShiftImmOp, LslOp);
  REPLACE(RShiftImmOp, LsrOp);
  REPLACE(RShiftImmLOp, LsrOp);
  REPLACE(GotoOp, BOp);
  REPLACE(GetGlobalOp, AdrOp);
  REPLACE(AndIOp, AndOp);
  REPLACE(OrIOp, OrOp);
  REPLACE(XorIOp, EorOp);
  REPLACE(AddFOp, FaddOp);
  REPLACE(SubFOp, FsubOp);
  REPLACE(MulFOp, FmulOp);
  REPLACE(DivFOp, FdivOp);
  REPLACE(F2IOp, FcvtzsOp);
  REPLACE(I2FOp, ScvtfOp);

  runRewriter([&](ModIOp *op) {
    auto denom = op->getOperand(0);
    auto nom = op->getOperand(1);

    builder.setBeforeOp(op);
    auto quot = builder.create<SdivWOp>(op->getOperands(), op->getAttrs());
    builder.replace<MlaOp>(op, { quot, nom, denom });
    return true;
  });
  
}
