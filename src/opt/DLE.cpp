#include "CleanupPasses.h"

using namespace sys;

#define STORE_ADDR(op) op->DEF(1)
#define LOAD_ADDR(op)  op->DEF()

std::map<std::string, int> DLE::stats() {
  return {
    { "removed-loads", elim }
  };
}

void DLE::runImpl(Region *region) {
  // First have a simple, context-insensitive approach to deal with load-after-store.
  std::map<Op*, Op*> replacement;

  for (auto bb : region->getBlocks()) {
    std::vector<Op*> liveStore;
    auto ops = bb->getOps();

    for (auto op : ops) {
      if (isa<StoreOp>(op)) {
        std::vector<Op*> newStore { op };
        for (auto x : liveStore) {
          if (neverAlias(STORE_ADDR(x), STORE_ADDR(op)))
            newStore.push_back(x);
        }
        liveStore = std::move(newStore);
        continue;
      }
      
      // This call might invalidate all living stores.
      // We can have more granularity but I don't want to do it now.
      if (isa<CallOp>(op) && op->has<ImpureAttr>()) {
        liveStore.clear();
        continue;
      }

      if (isa<LoadOp>(op)) {
        // Replaces the loaded value with the init value of store.
        for (auto x : liveStore) {
          auto init = x->getOperand(0).defining;
          auto storeAddr = x->getOperand(1).defining;
          auto loadAddr = op->getOperand().defining;
          if (mustAlias(LOAD_ADDR(op), STORE_ADDR(x)) || storeAddr == loadAddr) {
            op->replaceAllUsesWith(init);
            op->erase();
            elim++;
            break;
          }
        }
      }
    }
  }

  std::map<BasicBlock*, std::set<Op*>> liveIn;
  std::map<BasicBlock*, std::set<Op*>> liveOut;

  const auto &blocks = region->getBlocks();
  std::vector<BasicBlock*> worklist(blocks.begin(), blocks.end());

  while (!worklist.empty()) {
    BasicBlock *bb = worklist.back();
    worklist.pop_back();

    // Live in should be the INTERSECTION of all live out.
    // Unlike liveness analysis, it isn't union here.
    std::set<Op*> newLiveIn;

    bool firstPred = true;
    for (auto pred : bb->preds) {
      if (firstPred) {
        newLiveIn = liveOut[pred];
        firstPred = false;
      } else {
        // Compute intersection.
        std::set<Op*> temp;
        for (Op* op : newLiveIn) {
          if (liveOut[pred].count(op))
            temp.insert(op);
        }
        newLiveIn = std::move(temp);
      }
    }

    if (newLiveIn != liveIn[bb])
      liveIn[bb] = newLiveIn;

    std::set<Op*> live = liveIn[bb];

    auto ops = bb->getOps();
    for (auto op : ops) {
      if (isa<StoreOp>(op)) {
        // Kill all loads in `live` that might alias with the store.
        Op *storeAddr = STORE_ADDR(op);

        for (auto it = live.begin(); it != live.end(); ) {
          Op *load = *it;
          Op *loadAddr = LOAD_ADDR(load);
          if (mayAlias(storeAddr, loadAddr))
            it = live.erase(it);
          else
            ++it;
        }
      }

      // Note that there might be some redundancy, but it doesn't matter.
      // Also, we're pulling `load` in `live` rather than the address. That makes it easier for rewriting.
      if (isa<LoadOp>(op)) {
        // Check if something is exactly the value of `addr`, or must alias with it.
        // Note that the value might not `mustAlias` with itself; 
        // for example it might have `<alloca %1, -1>` which doesn't mustAlias with anything.
        Op *addr = LOAD_ADDR(op);
        bool replaced = false;
        for (auto load : live) {
          if (mustAlias(LOAD_ADDR(load), addr) || load->getOperand().defining == addr) {
            op->replaceAllUsesWith(load);
            op->erase();
            elim++;
            replaced = true;
            break;
          }
        }

        if (!replaced)
          live.insert(op);
      }
    }

    if (live != liveOut[bb]) {
      liveOut[bb] = live;

      for (auto succ : bb->succs)
        worklist.push_back(succ);
    }
  }
}

void DLE::run() {
  auto funcs = collectFuncs();

  for (auto func : funcs)
    runImpl(func->getRegion());
}
