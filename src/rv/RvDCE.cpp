#include "RvPasses.h"

using namespace sys::rv;
using namespace sys;

std::map<std::string, int> RvDCE::stats() {
  return {
    { "eliminated-ops", elim }
  };
}

bool RvDCE::isImpure(Op *op) {
  if (isa<SubSpOp>(op) || isa<JOp>(op) ||
      isa<BneOp>(op) || isa<BltOp>(op) ||
      isa<BgeOp>(op) || isa<BeqOp>(op) || isa<WriteRegOp>(op) ||
      isa<StoreOp>(op) || isa<RetOp>(op) ||
      isa<CallOp>(op))
    return true;

  return false;
}

// Here no nested regions are possible.
void RvDCE::markImpure(Region *region) {
  for (auto bb : region->getBlocks()) {
    for (auto op : bb->getOps()) {
      if (isImpure(op) && !op->has<ImpureAttr>())
        op->add<ImpureAttr>();
    }
  }
}

void RvDCE::runOnRegion(Region *region) {
  for (auto bb : region->getBlocks()) {
    for (auto op : bb->getOps()) {
      if (!op->has<ImpureAttr>() && op->getUses().size() == 0)
        removeable.push_back(op);
      else for (auto r : op->getRegions())
        runOnRegion(r);
    }
  }
}

void RvDCE::run() {
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
