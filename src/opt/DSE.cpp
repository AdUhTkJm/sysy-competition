#include "CleanupPasses.h"
#include "Analysis.h"
#include <deque>

using namespace sys;

std::map<std::string, int> DSE::stats() {
  return {
    { "removed-stores", elim },
  };
}

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
    for (auto pred : bb->preds)
      live.insert(out[pred].begin(), out[pred].end());

    auto oldOut = out[bb];
    auto curLive = live;

    for (auto op : bb->getOps()) {
      if (isa<LoadOp>(op)) {
        for (auto *store : curLive) {
          auto addr = store->getOperand(1).defining;
          if (mayAlias(addr, op->DEF()))
            used[store] = true;
        }
      }

      if (isa<StoreOp>(op)) {
        std::vector<Op*> killed;
        for (auto store : curLive) {
          auto addr = store->getOperand(1).defining;
          // If this op stores to the exact same place as one of the `live` ops,
          // then that store is no longer live.
          if (mustAlias(addr, op->DEF(1)))
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
      for (auto succ : bb->succs)
        worklist.push_back(succ);
    }
  }


  auto funcOp = region->getParent();

  // Find allocas used by calls.
  // Stores to them shouldn't be eliminated. (This is also a kind of escape analysis.)
  auto calls = funcOp->findAll<CallOp>();
  std::set<Op*> outref;
  for (auto call : calls) {
    for (auto operand : call->getOperands()) {
      auto def = operand.defining;
      if (auto attr = def->find<AliasAttr>()) {
        for (auto &[base, offsets] : attr->location) {
          if (isa<AllocaOp>(base))
            outref.insert(base);
        }
      }
    }
  }

  // Eliminate unused stores.
  auto allStores = funcOp->findAll<StoreOp>();
  for (auto *store : allStores) {
    if (used[store])
      continue;

    auto addr = store->getOperand(1).defining;
    if (!addr->has<AliasAttr>())
      continue;
    
    auto alias = ALIAS(addr);
    // Never eliminate unknown things.
    bool canElim = !alias->unknown;
    for (auto [base, _] : alias->location) {
      // We can only eliminate if this stores to a local variable.
      // If parent of `base` is a ModuleOp (i.e. global), or another function,
      // then it's not a candidate of removal.
      if (base->getParentOp() != funcOp) {
        canElim = false;
        break;
      }
      // Don't remove stores to escaped locals.
      if (outref.count(base)) {
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
  Alias(module).run();
  
  auto funcs = collectFuncs();

  for (auto func : funcs)
    runImpl(func->getRegion());
}
