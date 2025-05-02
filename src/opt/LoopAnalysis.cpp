#include "LoopPasses.h"

using namespace sys;

LoopForest LoopAnalysis::runImpl(Region *region) {
  region->updateDoms();

  LoopForest forest;
  for (auto bb : region->getBlocks()) {
    for (auto succ : bb->getSuccs()) {
      if (!bb->dominatedBy(succ))
        continue;
      
      // `bb` is dominated by `succ`; this is a backedge.

      // The header is already seen (as the loop has multiple latches).
      // Skip it.
      if (forest.loopMap.count(succ))
        continue;

      // Note that the loop is a strongly connected component in which every two blocks can reach each other.
      // Therefore all blocks dominated by `header` that can reach `header` form this loop.
      LoopInfo *info = new LoopInfo;
      auto header = succ;
      info->header = header;
      for (auto suspect : region->getBlocks()) {
        if (suspect->dominatedBy(header) && suspect->reachable(header)) {
          info->bbs.insert(suspect);

          if (suspect->getSuccs().count(succ))
            info->latches.push_back(suspect);
        }
      }
      
      // Find exit and exiting blocks.
      for (auto loopbb : info->bbs) {
        bool exiting = false;
        for (auto succ : loopbb->getSuccs()) {
          if (!info->contains(succ)) {
            exiting = true;
            info->exits.push_back(succ);
            break;
          }
        }
        if (exiting)
          info->exitings.push_back(loopbb);
      }
      // Find if itself is nested over some other loop.
      std::vector<BasicBlock*> candidates;
      for (auto [head, info] : forest.loopMap) {
        if (info->contains(header))
          candidates.push_back(head);
      }

      // Find the deepest nest: the one contained in all other loops.
      for (auto x : candidates) {
        bool direct = true;
        for (auto y : candidates) {
          if (x != y && !x->dominatedBy(y)) {
            direct = false;
            break;
          }
        }
        if (direct) {
          // Now `x` is the direct parent of the loop `info`.
          LoopInfo *parentInfo = forest.getInfo(x);
          info->parent = parentInfo;
          parentInfo->subloops.push_back(info);
          break;
        }
      }

      forest.loopMap[succ] = info;
      forest.loops.push_back(info);
    }
  }

  return forest;
}

void LoopAnalysis::run() {
  auto funcs = collectFuncs();
  for (auto func : funcs)
    info[func] = runImpl(func->getRegion());
}
