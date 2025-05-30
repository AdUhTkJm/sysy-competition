#include "PreLoopPasses.h"

using namespace sys;

void Lower::run() {
  auto loops = module->findAll<ForOp>();

  Builder builder;
  // Destruct fors and turn them into whiles.
  for (auto loop : loops) {
    builder.setBeforeOp(loop);
    auto ivAddr = loop->getOperand(3);

    // Put a load of the address at the entry.
    auto region = loop->getRegion();
    builder.setToRegionStart(region);
    auto iv = builder.create<LoadOp>(Value::i32, { ivAddr }, { new SizeAttr(4) });

    // Replace the induction variable with the load.
    loop->replaceAllUsesWith(iv);

    // Before every break/continue, insert a store `iv = iv + 'a`.
    auto terms = loop->findAll<BreakOp>();
    auto conts = loop->findAll<ContinueOp>();
    std::copy(conts.begin(), conts.end(), std::back_inserter(terms));
    auto incr = loop->getOperand(2);

    for (auto op : terms) {
      builder.setBeforeOp(op);
      auto add = builder.create<AddIOp>({ iv, incr });
      builder.create<StoreOp>({ add, ivAddr });
    }

    // Also do it at the end.
    auto last = region->getLastBlock();
    builder.setToBlockEnd(last);
    auto add = builder.create<AddIOp>({ iv, incr });
    builder.create<StoreOp>({ add, ivAddr });

    // Create a while loop.
    builder.setBeforeOp(loop);
    auto wloop = builder.create<WhileOp>();
    auto before = wloop->appendRegion();
    auto after = wloop->appendRegion();

    // Move all blocks in the for to the after region.
    for (auto it = region->begin(); it != region->end();) {
      auto next = it; next++;
      (*it)->moveToEnd(after);
      it = next;
    }

    // Create the condition at the before region.
    before->appendBlock();
    builder.setToRegionStart(before);
    auto load = builder.create<LoadOp>(Value::i32, { ivAddr }, { new SizeAttr(4) });
    auto stop = loop->getOperand(1);
    auto lt = builder.create<LtOp>({ load, stop });
    builder.create<ProceedOp>({ lt });

    // Create the start value before the while.
    builder.setBeforeOp(wloop);
    auto start = loop->getOperand(0);
    builder.create<StoreOp>({ start, ivAddr }, { new SizeAttr(4) });

    // Erase the ForOp.
    loop->erase();
  }
}
