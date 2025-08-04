#include "PreAnalysis.h"

using namespace sys;

#define BAD(cond) if (cond) return

void Parallelizable::runImpl(Op *loop, int depth) {
  NoStore(module).run();
  auto fnMap = getFunctionMap();
  // Find deeper loops inside the current one.
  auto region = loop->getRegion();
  auto entry = region->getFirstBlock();
  for (auto op : entry->getOps()) {
    if (isa<ForOp>(op))
      runImpl(op, depth + 1);
  }
  // Cannot call impure functions, unless the function never stores.
  auto calls = loop->findAll<CallOp>();
  for (auto call : calls) {
    const auto &name = NAME(call);
    BAD(call->has<ImpureAttr>() && (isExtern(name) || !fnMap[name]->has<NoStoreAttr>()));
  }
  // Cannot have early return.
  if (loop->findAll<ReturnOp>().size())
    return;

  // The subscript for this variable `loop` must be the same.
  std::unordered_map<Op*, std::vector<std::pair<Op*, bool>>> access, ops;
  auto stores = loop->findAll<StoreOp>();
  for (auto store : stores) {
    auto addr = store->DEF(1);
    BAD(!addr->has<BaseAttr>());
    access[BASE(addr)].emplace_back(addr, true);
    ops[BASE(addr)].emplace_back(store, true);
  }

  auto loads = loop->findAll<LoadOp>();
  for (auto load : loads) {
    auto addr = load->DEF();
    BAD(!addr->has<BaseAttr>());
    access[BASE(addr)].emplace_back(addr, false);
    ops[BASE(addr)].emplace_back(load, false);
  }

  // Stores that aren't nested in inner loops.
  std::set<Op*> directStores;
  for (auto op : region->getFirstBlock()->getOps()) {
    if (isa<StoreOp>(op))
      directStores.insert(BASE(op->DEF(1)));
  }

  for (const auto &[base, access] : access) {
    // Check subscript.
    assert(access.size());
    auto [addr, isStore] = access[0];
    if (!addr->has<SubscriptAttr>()) {
      // Acceptable if this scalar is only accessed in nested loops.
      if (!directStores.count(addr))
        continue;
      // We can accept loads as long as there's no stores into it.
      for (auto [_, isStore] : access)
        BAD(isStore);
      continue;
    }

    // Similarly, we can accept loads to arrays when there are no stores.
    bool hasStore = false;
    for (auto [_, isStore] : access) {
      if (isStore) {
        hasStore = true;
        break;
      }
    }
    if (!hasStore)
      continue;

    // The stride and constant of current loop.
    const auto &subscript = SUBSCRIPT(addr);
    auto n = subscript[depth];
    auto vi = n ? subscript.back() / (n / 4) : -1;

    for (auto [addr, _] : access) {
      BAD(!addr->has<SubscriptAttr>());
      const auto &subscript = SUBSCRIPT(addr);
      auto n2 = subscript[depth];
      auto vi2 = n2 ? subscript.back() / (n2 / 4) : -1;
      BAD(n2 != n || vi2 != vi);
    }
  }

  // The first load must have a preceding store, or no store at all.
  for (const auto &[base, access] : ops) {
    Op *load = nullptr;
    for (auto [op, isStore] : access) {
      if (!isStore) {
        load = op;
        break;
      }
    }
    // Alright. No stores, or no loads.
    if (!load || load == access[0].first)
      continue;
    
    auto [store, _] = access[0];
    std::vector<Op*> parents;
    for (auto runner = store; runner != loop; runner = runner->getParentOp())
      parents.push_back(runner);
    for (auto runner = load; runner != loop; runner = runner->getParentOp()) {
      // check whether `runner` and anything in `parents` are on the same layer.
      bool decided = false, good = false;
      for (auto parent : parents) {
        if (runner->getParent() != parent->getParent())
          continue;

        decided = true;
        // We expect `parent` to be front of `runner`.
        for (auto w = parent; !w->atBack(); w = w->nextOp()) {
          if (w == runner) {
            good = true;
            break;
          }
        }
        if (parent->getParent()->getLastOp() == runner) {
          good = true;
          break;
        }
      }
      if (decided && !good)
        return;
      if (decided)
        break;
    }
  }

  // Now it's parallelizable.
  loop->add<ParallelizableAttr>();
}

void Parallelizable::run() {
  ArrayAccess(module).run();
  Base(module).run();

  auto funcs = collectFuncs();
  for (auto func : funcs) {
    auto region = func->getRegion();
    // Clear stale data.
    for (auto bb : region->getBlocks()) {
      for (auto op : bb->getOps())
        op->remove<ParallelizableAttr>();
    }

    for (auto bb : region->getBlocks()) {
      for (auto op : bb->getOps()) {
        if (isa<ForOp>(op))
          runImpl(op, 0);
      }
    }
  }
}
