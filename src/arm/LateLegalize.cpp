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
}
