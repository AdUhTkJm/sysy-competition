#include "RvPasses.h"
#include "RvOps.h"
#include "../codegen/Attrs.h"

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
      isa<BgeOp>(op) || isa<BnezOp>(op) ||
      isa<BeqOp>(op) || isa<WriteRegOp>(op) ||
      isa<StoreOp>(op) || isa<RetOp>(op) ||
      isa<BezOp>(op) || isa<CallOp>(op))
    return true;

  return false;
}

bool RvDCE::markImpure(Region *region) {
  bool impure = false;
  for (auto bb : region->getBlocks()) {
    for (auto op : bb->getOps()) {
      bool opImpure = false;
      if (isImpure(op))
        opImpure = true;
      for (auto r : op->getRegions())
        opImpure |= markImpure(r);

      if (opImpure && !op->hasAttr<ImpureAttr>()) {
        impure = true;
        op->addAttr<ImpureAttr>();
      }
    }
  }
  return impure;
}

void RvDCE::runOnRegion(Region *region) {
  markImpure(region);
  for (auto bb : region->getBlocks()) {
    for (auto op : bb->getOps()) {
      if (!op->hasAttr<ImpureAttr>() && op->getUses().size() == 0)
        removeable.push_back(op);
      else for (auto r : op->getRegions())
        runOnRegion(r);
    }
  }
}

void RvDCE::run() {
  auto funcs = module->findAll<FuncOp>();
  for (auto func : funcs)
    runOnRegion(func->getRegion());

  elim = removeable.size();
  for (auto op : removeable)
    op->erase();
}
