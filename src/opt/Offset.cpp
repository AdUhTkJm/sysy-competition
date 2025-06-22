#include "CleanupPasses.h"

using namespace sys;

std::map<std::string, int> Offset::stats() {
  return {
    { "folded-offsets", folded }
  };
}

// Works for each single block.
bool Offset::runImpl(Region *region) {
  Builder builder;

  bool changed = false;
  for (auto bb : region->getBlocks()) {
    std::unordered_map<Op*, Op*> basemap;

    auto ops = bb->getOps();
    for (auto op : ops) {
      if (!isa<AddLOp>(op))
        continue;

      auto base = op->DEF(0);
      if (basemap.count(base)) {
        auto offset = basemap[base]->getOperand(1);
        folded++;

        builder.setBeforeOp(op);
        Value diff = builder.create<SubIOp>({ op->DEF(1), offset });
        builder.replace<AddLOp>(op, { basemap[base], diff });
        changed = true;
        continue;
      }
      basemap[base] = op;
    }
  }
  return changed;
}

void Offset::run() {
  auto funcs = collectFuncs();
  
  for (auto func : funcs) {
    auto region = func->getRegion();
    while (runImpl(region));
  }
}
