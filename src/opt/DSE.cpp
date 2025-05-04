#include "Passes.h"
#include <deque>

using namespace sys;

std::map<std::string, int> DSE::stats() {
  return {
    { "removed-stores", elim },
  };
}

#if 0
void DSE::runImpl(Region *region) {
  used.clear();

  DomTree tree = getDomTree(region);
  std::set<Op*> live;


  auto funcOp = region->getParent();
  BasicBlock *bb = region->getFirstBlock();
  // Only checks the initialization part. The dataflow approach is astonishingly imprecise.
  for (;;) {
    for (auto op : bb->getOps()) {
      if (isa<StoreOp>(op)) {
        // If this op stores to the exact same place as one of the `live` ops,
        // then that store is no longer live.
        std::vector<Op*> killed;
        for (auto liveOp : live) {
          auto liveAlias = ALIAS(liveOp->getOperand(1).defining);
          auto opAlias = ALIAS(op->getOperand(1).defining);

          if (liveAlias->mustAlias(opAlias)) {
            killed.push_back(liveOp);
            if (!used[liveOp]) {
              // Able to erase it, but...
              auto loc = ALIAS(liveOp->getOperand(1).defining)->location;
              bool canElim = true;
              for (auto [base, _] : loc) {
                // We can only eliminate if this stores to a local variable.
                // If parent of `base` is a ModuleOp (i.e. global), or another function,
                // then it's not a candidate of removal.
                if (base->getParentOp() != funcOp) {
                  canElim = false;
                  break;
                }
              }
              if (!canElim)
                continue;

              elim++;
              liveOp->erase();
            }
          }
        }
        for (auto kill : killed)
          live.erase(kill);

        live.insert(op);
      }
  
      if (isa<LoadOp>(op)) {
        for (auto liveOp : live) {
          if (ALIAS(liveOp->getOperand(1).defining)->mayAlias(ALIAS(op->getOperand().defining)))
            used[liveOp] = true;
        }
      }
    }

    if (bb->getSuccs().size() != 1)
      break;
    
    BasicBlock *succ = *bb->getSuccs().begin();
    if (succ->getPreds().size() != 1)
      break;

    bb = succ;
  }
}
#endif

void DSE::runImpl(Region *region) {
  used.clear();
  // Use a dataflow approach.
  // If it's wrong, then switch back to the coarse approach similar to Globalization.
  std::map<BasicBlock *, std::set<Op*>> in, out;
  const auto &bbs = region->getBlocks();
  std::deque<BasicBlock*> worklist(bbs.begin(), bbs.end());

  // Note that this is a FORWARD dataflow, rather than backward (as in updateLiveness()).
  // That's what ate up my whole afternoon.
  while (!worklist.empty()) {
    BasicBlock *bb = worklist.front();
    worklist.pop_front();

    // liveIn[bb] = \bigcup liveOut[pred]
    std::set<Op*> live;
    for (auto pred : bb->getPreds())
      live.insert(out[pred].begin(), out[pred].end());

    auto oldOut = out[bb];
    auto curLive = live;

    for (auto op : bb->getOps()) {
      if (isa<LoadOp>(op)) {
        for (auto *store : curLive) {
          auto addr = store->getOperand(1).defining;
          if (ALIAS(addr)->mayAlias(ALIAS(op->getOperand().defining)))
            used[store] = true;
        }
      }

      if (isa<StoreOp>(op)) {
        std::vector<Op*> killed;
        for (auto store : curLive) {
          auto addr = store->getOperand(1).defining;
          // If this op stores to the exact same place as one of the `live` ops,
          // then that store is no longer live.
          if (ALIAS(addr)->mustAlias(ALIAS(op->getOperand(1).defining)))
            killed.push_back(store);
        }
        for (auto kill : killed)
          curLive.erase(kill);

        // Don't forget to add this store to live set.
        curLive.insert(op);
      }
    }

    // Update if `out` changed.
    if (curLive != out[bb]) {
      out[bb] = curLive;
      for (auto succ : bb->getSuccs())
        worklist.push_back(succ);
    }
  }

  // Eliminate unused stores.
  auto funcOp = region->getParent();
  auto allStores = funcOp->findAll<StoreOp>();
  for (auto *store : allStores) {
    if (used[store])
      continue;

    bool canElim = true;
    for (auto [base, _] : ALIAS(store->getOperand(1).defining)->location) {
      // We can only eliminate if this stores to a local variable.
      // If parent of `base` is a ModuleOp (i.e. global), or another function,
      // then it's not a candidate of removal.
      if (base->getParentOp() != funcOp) {
        canElim = false;
        break;
      }
    }

    if (!canElim)
      continue;

    elim++;
    store->erase();
  }
}

void DSE::run() {
  auto funcs = collectFuncs();

  for (auto func : funcs)
    runImpl(func->getRegion());
}
