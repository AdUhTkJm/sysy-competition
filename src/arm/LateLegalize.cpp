#include "ArmPasses.h"

using namespace sys;
using namespace sys::arm;

void LateLegalize::run() {
  Builder builder;
  
  // ARM does not support `add x0, xzr, 1`.
  runRewriter([&](AddXIOp *op) {
    if (RS(op) == Reg::xzr)
      builder.replace<MovIOp>(op, { RDC(RD(op)), new IntAttr(V(op)) });
    
    return false;
  });

  runRewriter([&](AddWIOp *op) {
    if (RS(op) == Reg::xzr)
      builder.replace<MovIOp>(op, { RDC(RD(op)), new IntAttr(V(op)) });
    
    return false;
  });

  // Use `mov` and `movk` for an out-of-range `mov`.
  runRewriter([&](MovIOp *op) {
    if (std::abs(V(op)) >= 16384) {
      unsigned v = V(op);

      builder.setBeforeOp(op);
      builder.create<MovIOp>({ RDC(RD(op)), new IntAttr(v & 0xffff) });
      builder.replace<MovkOp>(op, { RDC(RD(op)), new IntAttr((v) >> 16), new LslAttr(16) });
    }
    return false;
  });
}
