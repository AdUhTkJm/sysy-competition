#include "PrePasses.h"
#include "PreAnalysis.h"

using namespace sys;

std::map<std::string, int> TidyMemory::stats() {
  return {
    { "tidied-ops", tidied }
  };
}

void TidyMemory::runImpl(Region *region) {
  // Maps stored addresses into the value.
  std::unordered_map<Op*, Op*> values;
  for (auto bb : region->getBlocks()) {
    auto ops = bb->getOps();
    for (auto op : ops) {
      // Conservatively invalidates all stores.
      if (op->getRegionCount()) {
        values.clear();
        for (auto r : op->getRegions())
          runImpl(r);
        continue;
      }

      if (isa<CallOp>(op) && op->has<ImpureAttr>()) {
        values.clear();
        continue;
      }

      if (isa<StoreOp>(op)) {
        auto addr = op->DEF(1);
        auto val = op->DEF(0);
        if (isa<LoadOp>(val) || isa<CallOp>(val) || isa<GetArgOp>(val)) {
          values.erase(addr);
          continue;
        }

        // Have to make sure it's not an array;
        // otherwise there's no way to detect whether another thing aliases with it.
        if (!addr->has<BaseAttr>()) {
          values.clear();
          continue;
        }
        auto base = BASE(addr);
        if (!isa<AllocaOp>(base) || SIZE(base) != 4)
          continue;

        values[addr] = val;
        continue;
      }

      if (isa<LoadOp>(op) && values.count(op->DEF())) {
        op->replaceAllUsesWith(values[op->DEF()]);
        op->erase();
        tidied++;
        continue;
      }
    }
  }
}

void TidyMemory::run() {
  Base(module).run();
  auto funcs = collectFuncs();

  for (auto func : funcs)
    runImpl(func->getRegion());
}