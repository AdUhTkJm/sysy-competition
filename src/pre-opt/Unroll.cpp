#include "PreLoopPasses.h"

using namespace sys;

std::map<std::string, int> Unroll::stats() {
  return {
    { "unrolled-loops", unrolled },
  };
}

namespace {

bool innermost(Op *loop) {
  auto region = loop->getRegion();
  auto entry = region->getFirstBlock();
  for (auto op : entry->getOps()) {
    if (isa<WhileOp>(op) || isa<ForOp>(op))
      return false;
  }
  return true;
}

}

// Defined in Unswitch.cpp.
void unroll(Op *loop, int vi);

// Defined in EarlyInline.cpp.
int opcount(Region *region);

void Unroll::run() {
  auto loops = module->findAll<ForOp>();
  for (auto loop : loops) {
    // Only unroll innermost loops.
    if (!innermost(loop))
      continue;

    // Don't unroll large loops.
    auto region = loop->getRegion();
    if (opcount(region) >= 50)
      continue;

    // Don't unroll loops with calls in it.
    if (loop->findAll<CallOp>().size() > 0)
      continue;

    // unroll() requires that step is 1.
    auto step = loop->DEF(2);
    if (!isa<IntOp>(step) || V(step) != 1)
      continue;

    // Unroll it twice.
    unroll(loop, 2);
    unrolled++;
  }
}
