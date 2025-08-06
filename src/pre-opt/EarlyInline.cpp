#include "PrePasses.h"

using namespace sys;

namespace {

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

void EarlyInline::run() {
  auto funcs = collectFuncs();
  Builder builder;

  for (auto func : funcs) {
    auto region = func->getRegion();

    // Inline very small functions only.
    // But if the function is <once>'d, then we might also inline it.
    if (opcount(region) >= 200 && !func->has<AtMostOnceAttr>())
      continue;

    // Don't inline recursive functions.
    if (isRecursive(func))
      continue;

    // We only have structured control flow.
    // We can't support arbitrary returns in pre-passes.
    auto rets = func->findAll<ReturnOp>();
    if (rets.size() > 1) {
      // One more thing inlinable is when the 2 ops lie in the same parent op's
      // different regions, and the parent is at back.
      if (rets.size() == 2) {
        auto p1 = rets[0]->getParentOp(), p2 = rets[1]->getParentOp();
        if (rets[0]->atBack() && rets[1]->atBack() && p1 == p2 && p1->atBack() && p1->getParentOp() == func) {
          // OK, inlinable. Sink the returns to the end and goto the main path.
          Builder builder;
          builder.setBeforeOp(p1);
          auto alloca = builder.create<AllocaOp>({ new SizeAttr(4) });
          auto retTy = rets[0]->getResultType();
          if (retTy == Value::f32)
            alloca->add<FPAttr>();
          builder.replace<StoreOp>(rets[0], { rets[0]->getOperand(), alloca }, { new SizeAttr(4) });
          builder.replace<StoreOp>(rets[1], { rets[1]->getOperand(), alloca }, { new SizeAttr(4) });

          builder.setAfterOp(p1);
          auto load = builder.create<LoadOp>(retTy, { alloca });
          builder.create<ReturnOp>({ load });
          goto mainpath;
        }
      }
      continue;
    }
    if (rets.size() && rets[0]->getParent() != region->getLastBlock())
      continue;

    mainpath:
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
