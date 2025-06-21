#include "LoopPasses.h"

using namespace sys;

std::map<std::string, int> RemoveEmptyLoop::stats() {
  return {
    { "removed-loops", removed }
  };
}

#define PINNED(Ty) || isa<Ty>(op)
static bool pinned(Op *op) {
  return (isa<CallOp>(op) && op->has<ImpureAttr>())
    PINNED(StoreOp);
}

bool RemoveEmptyLoop::runImpl(LoopInfo *info) {
  if (info->exits.size() != 1)
    return false;

  for (auto bb : info->getBlocks()) {
    for (auto op : bb->getOps()) {
      // Side-effect.
      if (pinned(op))
        return false;

      // Something is used outside, cannot remove.
      for (auto use : op->getUses()) {
        if (!info->contains(use->getParent()))
          return false;
      }
    }
  }

  // Safe to remove.
  // All header's predecessors should now connect to the exit.
  auto header = info->header;
  auto exit = info->getExit();
  for (auto pred : header->preds) {
    auto term = pred->getLastOp();
    if (TARGET(term) == header)
      TARGET(term) = exit;
    if (term->has<ElseAttr>() && ELSE(term) == header)
      ELSE(term) = exit;
  }
  
  for (auto bb : info->getBlocks()) {
    for (auto op : bb->getOps())
      op->removeAllOperands();
  }
  auto bbs = info->getBlocks();
  for (auto bb : bbs)
    bb->forceErase();

  removed++;
  return true;
}

void RemoveEmptyLoop::run() {
  LoopAnalysis analysis(module);
  auto funcs = collectFuncs();

  for (auto func : funcs) {
    auto region = func->getRegion();
    auto forest = analysis.runImpl(region);

    bool changed;
    do {
      changed = false;
      for (auto loop : forest.getLoops()) {
        if (!runImpl(loop))
          continue;

        forest = analysis.runImpl(region);
        changed = true;
        break;
      }
    } while (changed);
  }
}
