#include "PrePasses.h"

using namespace sys;

void Remerge::runImpl(Region *region) {
  auto entry = region->getFirstBlock();
  const auto &bbs = region->getBlocks();
  for (auto bb : bbs) {
    if (bb != entry)
      bb->moveAllOpsTo(entry);
  }
  for (auto it = --bbs.end(); it != bbs.begin();) {
    auto next = it; --next;
    (*it)->erase();
    it = next;
  }

  // Recursively find operations with regions.
  for (auto op : entry->getOps()) {
    if (op->getRegionCount()) {
      for (auto x : op->getRegions())
        runImpl(x);
    }
  }
}

void Remerge::run() {
  auto funcs = collectFuncs();

  for (auto func : funcs)
    runImpl(func->getRegion());

  MoveAlloca(module).run();
}
