#include "PreLoopPasses.h"

using namespace sys;

std::map<std::string, int> LoopDCE::stats() {
  return {
    { "erased-loops", erased },
  };
}

namespace {

bool pure(Region *region) {
  if (!region->getBlocks().size())
    return true;

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
  Builder builder;
  bool changed;
  do {
    auto loops = module->findAll<ForOp>();
    changed = false;
    for (auto loop : loops) {
      auto region = loop->getRegion();
      if (pure(region)) {
        auto step = loop->DEF(2);
        if (!isa<IntOp>(step) || V(step) != 1)
          continue;
        
        // Replace with a store of `ivAddr`.
        builder.setAfterOp(loop);
        builder.create<StoreOp>({ loop->getOperand(1), loop->getOperand(3) }, { new SizeAttr(4) });
        loop->erase(), changed = true, erased++;
        continue;
      }

      // For a parallel loop (no loop-carried dependency) whose indvar is never used,
      // It is simply doing repeated work.
      if (loop->has<ParallelizableAttr>() && !loop->getUses().size()) {
        if (region->getBlocks().size()) {
          auto entry = region->getFirstBlock();
          entry->inlineBefore(loop);
        }
        loop->erase(), changed = true, erased++;
        break;
      }
    }
  } while (changed);
}
