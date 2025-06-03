#include "PrePasses.h"

using namespace sys;

namespace {

// Count all ops inside a region.
int opcount(Region *region) {
  int total = 0;
  for (auto bb : region->getBlocks()) {
    for (auto op : bb->getOps()) {
      for (auto r : op->getRegions())
        total += opcount(r);
    }
    total += bb->getOpCount();
  }
  return total;
}

bool isRecursive(Op *func) {
  auto calls = func->findAll<CallOp>();
  const auto &name = NAME(func);
  for (auto call : calls) {
    if (NAME(call) == name)
      return true;
  }
  return false;
}

}

void EarlyInline::run() {
  auto funcs = collectFuncs();
  Builder builder;

  for (auto func : funcs) {
    // We only have structured control flow.
    // We can't support arbitrary returns in pre-passes.
    auto rets = func->findAll<ReturnOp>();
    auto region = func->getRegion();
    if (rets.size() > 1)
      continue;
    if (rets.size() && rets[0]->getParent() != region->getLastBlock())
      continue;

    // Inline very small functions only.
    if (opcount(region) >= 64)
      continue;

    // Don't inline recursive functions.
    if (isRecursive(func))
      continue;

    // Start rewriting.
    auto calls = module->findAll<CallOp>();
    std::unordered_map<Op*, Op*> cloneMap;

    const std::function<void (Op*)> copy = [&](Op *x) {
      auto copied = builder.copy(x);
      cloneMap[x] = copied;

      for (auto r : x->getRegions()) {
        Builder::Guard guard(builder);
        
        auto cr = copied->appendRegion();

        auto entry = r->getFirstBlock();
        auto cEntry = cr->appendBlock();
        builder.setToBlockStart(cEntry);
        for (auto op : entry->getOps())
          copy(op);
      }
    };

    const auto &name = NAME(func);
    for (auto call : calls) {
      if (NAME(call) != name)
        continue;

      cloneMap.clear();
      builder.setBeforeOp(call);

      // Copy function body.
      for (auto bb : region->getBlocks()) {
        for (auto op : bb->getOps())
          copy(op);
      }

      std::vector<Op*> getargs;
      Op *ret = nullptr;
      for (auto [_, v] : cloneMap) {
        if (isa<GetArgOp>(v))
          getargs.push_back(v);

        if (isa<ReturnOp>(v))
          ret = v;

        // Rewire operands.
        for (int i = 0; i < v->getOperandCount(); i++) {
          auto def = v->DEF(i);
          assert(cloneMap.count(def));
          v->setOperand(i, cloneMap[def]);
        }
      }

      // Replace arguments.
      for (auto get : getargs) {
        // Find the store (which should be the only use).
        assert(get->getUses().size() == 1);
        auto store = *get->getUses().begin();
        auto addr = store->DEF(1);

        // Get the actual argument.
        int vi = V(get);
        Op *arg = call->DEF(vi);

        auto uses = addr->getUses();
        int storecount = 0;
        for (auto use : uses) {
          if (isa<StoreOp>(use) && ++storecount >= 2)
            break;
        }
        // If the alloca is not a constant, we can only replace the getarg.
        if (storecount >= 2) {
          get->replaceAllUsesWith(arg);
          get->erase();
          continue;
        }

        // If the alloca is constant, then we can replace all loads.
        for (auto use : uses) {
          if (!isa<LoadOp>(use))
            continue;

          use->replaceAllUsesWith(arg);
          use->erase();
        }

        // Cleanup unused instructions.
        store->erase();
        addr->erase();
        get->erase();
      }

      // Replace return value.
      if (ret) {
        if (ret->getOperandCount())
          call->replaceAllUsesWith(ret->DEF());
        ret->erase();
      }
      call->erase();
    }
  }

  MoveAlloca(module).run();
}
