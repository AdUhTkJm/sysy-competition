#include "ArmOps.h"
#include "ArmPasses.h"

using namespace sys::arm;
using namespace sys;

std::map<std::string, int> ArmDCE::stats() {
  return {
    { "eliminated-ops", elim }
  };
}

#define IMPURE(Ty) || isa<Ty>(op)
bool ArmDCE::isImpure(Op *op) {
  return isa<AllocaOp>(op)
    IMPURE(StrFOp)
    IMPURE(StrWOp)
    IMPURE(StrXOp)
    IMPURE(StrFROp)
    IMPURE(StrWROp)
    IMPURE(StrXROp)
    IMPURE(BrOp)
    IMPURE(BgtOp)
    IMPURE(BltOp)
    IMPURE(BleOp)
    IMPURE(BeqOp)
    IMPURE(BneOp)
    IMPURE(BgeOp)
    IMPURE(BplOp)
    IMPURE(BmiOp)
    IMPURE(RetOp)
    IMPURE(CbzOp)
    IMPURE(CbnzOp)
    IMPURE(BOp)
    IMPURE(WriteRegOp)
  ;
}

// Here no nested regions are possible.
void ArmDCE::markImpure(Region *region) {
  for (auto bb : region->getBlocks()) {
    for (auto op : bb->getOps()) {
      if (isImpure(op) && !op->has<ImpureAttr>())
        op->add<ImpureAttr>();
    }
  }
}

void ArmDCE::runOnRegion(Region *region) {
  for (auto bb : region->getBlocks()) {
    for (auto op : bb->getOps()) {
      if (!op->has<ImpureAttr>() && op->getUses().size() == 0)
        removeable.push_back(op);
      else for (auto r : op->getRegions())
        runOnRegion(r);
    }
  }
}

void ArmDCE::run() {
  auto funcs = collectFuncs();
  for (auto func : funcs)
    markImpure(func->getRegion());
  
  do {
    removeable.clear();
    for (auto func : funcs)
      runOnRegion(func->getRegion());

    elim += removeable.size();
    for (auto op : removeable)
      op->erase();
  } while (removeable.size());
}
