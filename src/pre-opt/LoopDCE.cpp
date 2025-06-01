#include "PreLoopPasses.h"

using namespace sys;

std::map<std::string, int> LoopDCE::stats() {
  return {
    { "erased-loops", erased },
  };
}

namespace {

bool pure(Region *region) {
  auto entry = region->getFirstBlock();
  for (auto op : entry->getOps()) {
    if (op->has<ImpureAttr>())
      return false;
    for (auto x : op->getRegions()) {
      if (!pure(x))
        return false;
    }
  }
  return true;
}

}

void LoopDCE::run() {
  bool changed;
  do {
    auto loops = module->findAll<ForOp>();
    changed = false;
    for (auto loop : loops) {
      if (pure(loop->getRegion()))
        loop->erase(), changed = true, erased++;
    }
  } while (changed);
}
