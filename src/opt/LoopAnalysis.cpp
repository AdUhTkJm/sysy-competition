#include "LoopPasses.h"

using namespace sys;

LoopForest LoopAnalysis::runImpl(Region *region) {
  region->updateDoms();

  LoopForest forest;
  
  // Collect all blocks and latches in a loop.
  for (auto bb : region->getBlocks()) {
    for (auto succ : bb->getSuccs()) {
      if (!bb->dominatedBy(succ))
        continue;
      
      // `bb` is dominated by `succ`; this is a backedge.

      // The header is already seen (as the loop has multiple latches).
      // Skip it, but we have to update the loop info accordingly.
      LoopInfo *info;
      BasicBlock *header;
      if (forest.loopMap.count(succ)) {
        info = forest.loopMap[succ];
        info->bbs.insert(bb);
        header = info->header;
      } else {
        info = new LoopInfo;
        header = succ;
        info->header = header;
        info->bbs = { header, bb };
      }

      info->latches.insert(bb);

      std::vector<BasicBlock*> worklist { bb };
      while (!worklist.empty()) {
        auto back = worklist.back();
        worklist.pop_back();
        for (auto pred : back->getPreds()) {
          if (!info->bbs.count(pred)) {
            info->bbs.insert(pred);
            worklist.push_back(pred);
          }
        }
      }

      forest.loopMap[succ] = info;
    }
  }

  for (auto [k, v] : forest.loopMap)
    forest.loops.push_back(v);

  // Update more information of the loops: exit(ing)s, parent and preheader.
  for (auto info : forest.loops) {
    auto header = info->header;

    BasicBlock *preheader = nullptr;
    // Find preheader if there exists one.
    for (auto pred : header->getPreds()) {
      if (info->latches.count(pred))
        continue;
      if (preheader) {
        // At least 2 blocks can jump to `header`. Fail.
        preheader = nullptr;
        break;
      }
      preheader = pred;
    }
    info->preheader = preheader;
    
    // Find exit and exiting blocks.
    for (auto loopbb : info->bbs) {
      bool exiting = false;
      for (auto succ : loopbb->getSuccs()) {
        if (!info->contains(succ)) {
          exiting = true;
          info->exits.insert(succ);
          break;
        }
      }
      if (exiting)
        info->exitings.insert(loopbb);
    }

    // Find if itself is nested over some other loop.
    std::vector<BasicBlock*> candidates;
    for (auto [head, info] : forest.loopMap) {
      if (info->contains(header) && head != header)
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
  }

  return forest;
}

void LoopAnalysis::run() {
  auto funcs = collectFuncs();
  for (auto func : funcs)
    info[func] = runImpl(func->getRegion());
}
