#include "Passes.h"
#include "../codegen/Attrs.h"

using namespace sys;

std::map<std::string, int> DCE::stats() {
  return {
    { "eliminated-ops", elim }
  };
}

void DCE::runOnRegion(Region *region) {
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
