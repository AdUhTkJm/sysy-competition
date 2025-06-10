#include "PreAnalysis.h"

using namespace sys;

#define BAD(cond) if (cond) return

void Parallelizable::runImpl(Op *loop, int depth) {
  // Find deeper loops inside the current one.
  auto entry = loop->getRegion()->getFirstBlock();
  for (auto op : entry->getOps()) {
    if (isa<ForOp>(op))
      runImpl(op, depth + 1);
  }
  for (auto op : entry->getOps())
    BAD(isa<CallOp>(op) && op->has<ImpureAttr>());

  // The subscript for this variable `loop` must be the same.
  std::unordered_map<Op*, std::vector<std::pair<Op*, bool>>> access;
  auto stores = loop->findAll<StoreOp>();
  for (auto store : stores) {
    auto addr = store->DEF(1);
    BAD(!addr->has<BaseAttr>());
    access[BASE(addr)].emplace_back(addr, true);
  }

  auto loads = loop->findAll<LoadOp>();
  for (auto load : loads) {
    auto addr = load->DEF();
    BAD(!addr->has<BaseAttr>());
    access[BASE(addr)].emplace_back(addr, false);
  }

  for (const auto &[base, access] : access) {
    // Check subscript.
    assert(access.size());
    auto [addr, isStore] = access[0];
    if (!addr->has<SubscriptAttr>()) {
      // We can accept loads as long as there's no stores into it.
      for (auto [_, isStore] : access)
        BAD(isStore);
      continue;
    }

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

  // Now it's parallelizable.
  loop->add<ParallelizableAttr>();
}

void Parallelizable::run() {
  ArrayAccess(module).run();
  Base(module).run();

  auto funcs = collectFuncs();
  for (auto func : funcs) {
    auto region = func->getRegion();

    for (auto bb : region->getBlocks()) {
      for (auto op : bb->getOps()) {
        if (isa<ForOp>(op))
          runImpl(op, 0);
      }
    }
  }
}
