#include "PreLoopPasses.h"
#include "PreAnalysis.h"
#include "PrePasses.h"
#include "../opt/Analysis.h"

using namespace sys;

namespace {

bool parallelizable(Op *loop, std::unordered_map<Op*, Op*> &allocaMap) {
  if (!loop->has<ParallelizableAttr>())
    return false;

  auto loads = loop->findAll<LoadOp>();
  auto stores = loop->findAll<StoreOp>();
  std::set<Op*> stored;
  for (auto store : stores)
    stored.insert(store->DEF(1));

  for (auto load : loads) {
    auto addr = load->DEF();
    // Check for scalars that are only read but never stored.
    if (!isa<AllocaOp>(addr) || SIZE(addr) != 4 || stored.count(addr))
      continue;

    // Find init value.
    Op *init = nullptr;
    for (Op *runner = loop->prevOp(); !runner->atFront(); runner = runner->prevOp()) {
      if (isa<StoreOp>(runner) && runner->DEF(1) == addr)
        init = runner->DEF(0);
      if (isa<WhileOp>(runner) || isa<ForOp>(runner) || isa<IfOp>(runner))
        break;
    }
    // An alloca without a deterministic initial value.
    // If it isn't a constant we'll need to copy-paste the whole use-def chain,
    // which doesn't seem right (esp. when it involves loops).
    if (!init)
      return false;
    allocaMap[addr] = init;
  }

  return true;
}

}

int opcount(Region *region);

void Parallelize::run() {
  Parallelizable(module).run();
  Builder builder;

  // Identify parallelizable loops. We only want top-level ones.
  auto funcs = collectFuncs();
  std::vector<Op*> loops;
  for (auto func : funcs) {
    auto region = func->getRegion();

    for (auto bb : region->getBlocks()) {
      for (auto op : bb->getOps()) {
        if (isa<ForOp>(op))
          loops.push_back(op);
      }
    }
  }

  int cnt = 0;
  for (auto loop : loops) {
    // Maps allocas to their value before the loop.
    std::unordered_map<Op*, Op*> allocaMap;
    if (!parallelizable(loop, allocaMap))
      continue;
    if (opcount(loop->getRegion()) <= 100 && loop->findAll<CallOp>().empty() && loop->findAll<ForOp>().size() <= 1)
      continue;

    // Split the loop into two halves.
    // To avoid hassles, ensure the step is 1.
    auto step = loop->DEF(2);
    if (!isa<IntOp>(step) || V(step) != 1)
      continue;

    auto stop = loop->DEF(1), start = loop->DEF(0);
    builder.setBeforeOp(loop);

    // Avoid overflow: use (start - stop >> 1) + start.
    auto diff = builder.create<SubIOp>({ stop->getResult(), start });
    auto one = builder.create<IntOp>({ new IntAttr(1) });
    auto div = builder.create<RShiftOp>({ diff, one });
    auto newstop = builder.create<AddIOp>({ div, start });

    // For main thread, change `stop` to that value.
    loop->setOperand(1, newstop);

    // Create a new function.
    auto parent = loop->getParentOp<FuncOp>();
    const auto &name = NAME(parent);

    builder.setToRegionStart(module->getRegion());
    auto workername = "__worker_" + std::to_string(cnt++) + "_" + name;
    auto worker = builder.create<FuncOp>({
      new NameAttr(workername),
      new ArgCountAttr(0)
    });

    // Copy the loop and set proper bounds.
    std::unordered_map<Op*, Op*> cloneMap;
    std::unordered_set<Op*> cloned;

    builder.setToBlockStart(worker->appendRegion()->appendBlock());
    // We must create a new alloca for the induction variable.
    auto ivAddr = builder.create<AllocaOp>({ new SizeAttr(4) });

    // `newstop` will be marked as captured below.
    auto newone = builder.create<IntOp>({ new IntAttr(1) });
    auto copiedLoop = builder.create<ForOp>({ newstop, stop, step, ivAddr });

    // Copy the loop content.
    auto bbloop = loop->getRegion()->getFirstBlock();
    builder.setToBlockStart(copiedLoop->appendRegion()->appendBlock());
    
    const std::function<void (Op*)> copy = [&](Op *x) {
      auto copied = builder.copy(x);
      cloneMap[x] = copied;
      cloned.insert(copied);

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

    for (auto x : bbloop->getOps())
      copy(x);
    // Replace old induction var with the new one.
    cloneMap[loop] = copiedLoop;
    cloned.insert(copiedLoop);
    cloned.insert(ivAddr);

    // Collect captured values.
    builder.setBeforeOp(copiedLoop);
    std::unordered_set<Op*> captured { newstop, stop };
    for (auto [_, v] : cloneMap) {
      if (v == copiedLoop)
        continue;

      for (int i = 0; i < v->getOperandCount(); i++) {
        auto def = v->DEF(i);

        if (isa<IntOp>(def) || isa<GetGlobalOp>(def) || isa<AllocaOp>(def))
          cloneMap[def] = builder.copy(def);
        else if (!cloneMap.count(def))
          captured.insert(def);

        if (isa<AllocaOp>(def) && allocaMap.count(def)) {
          auto init = allocaMap[def];
          captured.insert(init);
        }
      }
    }

    // Resolve captured operations by promoting these to global variables.
    for (auto op : captured) {
      // Create a dedicated global for this value.
      builder.setToRegionStart(module->getRegion());
      auto fp = op->getResultType() == Value::f32;
      auto init = fp
        ? (Attr*) new IntArrayAttr(new int(0), 1)
        : (Attr*) new FloatArrayAttr(new float(0), 1);
      auto gname = "__worker_global_" + std::to_string(cnt++);
      auto global = builder.create<GlobalOp>({ init,
        new SizeAttr(4),
        new NameAttr(gname),
        new DimensionAttr({ 1 })
      });
      if (fp)
        global->add<FPAttr>();
      
      // Store it into the global.
      builder.setBeforeOp(loop);
      auto get = builder.create<GetGlobalOp>({ new NameAttr(gname) });
      builder.create<StoreOp>({ op, get }, { new SizeAttr(4) });
      
      // Create a load in the worker function.
      builder.setBeforeOp(copiedLoop);
      auto sideget = builder.create<GetGlobalOp>({ new NameAttr(gname) });
      auto load = builder.create<LoadOp>(op->getResultType(), { sideget }, { new SizeAttr(4) });
      cloneMap[op] = load;
    }

    // Replace again. This time we can safely ignore things that aren't in cloneMap.
    for (auto [_, v] : cloneMap) {
      for (int i = 0; i < v->getOperandCount(); i++) {
        auto def = v->DEF(i);
        if (cloneMap.count(def))
          v->setOperand(i, cloneMap[def]);
      }
    }
    for (auto [alloca, init] : allocaMap) {
      assert(cloneMap.count(init));
      builder.create<StoreOp>({ cloneMap[init]->getResult(), cloneMap[alloca] });
    }
    // Also replace the `stop` and `step` values.
    copiedLoop->setOperand(0, cloneMap[newstop]);
    copiedLoop->setOperand(1, cloneMap[stop]);
    copiedLoop->setOperand(2, newone);

    // Clone a thread.
    builder.setBeforeOp(loop);
    builder.create<CloneOp>({ new NameAttr(workername) });
    builder.setAfterOp(loop);
    builder.create<JoinOp>({ new NameAttr(workername) });

    // Create a WakeOp at the end.
    builder.setToRegionEnd(worker->getRegion());
    builder.create<WakeOp>({ new NameAttr(workername) });

    // Create globals that gets used by backend.
    builder.setToRegionStart(module->getRegion());
    builder.create<GlobalOp>({ new ImpureAttr,
      new IntArrayAttr(new int[2] { }, 2),
      new SizeAttr(8), // It might get localized when size = 4.
      new NameAttr("_lock" + workername)
    });
    builder.create<GlobalOp>({ new ImpureAttr,
      new IntArrayAttr(new int[2048] { }, 2048),
      new SizeAttr(8192),
      new NameAttr("_stack" + workername)
    });
  }

  MoveAlloca(module).run();
  CallGraph(module).run();
}
