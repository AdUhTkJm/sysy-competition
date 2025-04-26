#include "Passes.h"
#include "../codegen/Attrs.h"

using namespace sys;

std::map<std::string, int> DCE::stats() {
  return {
    { "eliminated-ops", elim }
  };
}

bool DCE::isImpure(Op *op) {
  if (isa<StoreOp>(op) || isa<ReturnOp>(op) ||
      isa<BranchOp>(op) || isa<GotoOp>(op) ||
      isa<ProceedOp>(op))
    return true;

  if (isa<CallOp>(op)) {
    auto name = op->getAttr<NameAttr>()->name;
    if (isExtern(name))
      return true;
    return findFunction(name)->hasAttr<ImpureAttr>();
  }

  return false;
}

bool DCE::markImpure(Region *region) {
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

void DCE::runOnRegion(Region *region) {
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

void DCE::run() {
  auto funcs = module->findAll<FuncOp>();
  for (auto func : funcs)
    runOnRegion(func->getRegion());

  elim = removeable.size();
  for (auto op : removeable)
    op->erase();
}
