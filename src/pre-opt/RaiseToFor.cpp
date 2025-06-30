#include "PreLoopPasses.h"
#include "PreAnalysis.h"
#include "../utils/Matcher.h"

using namespace sys;

static Rule forCond("(lt (load x) y)");
static Rule forCondLe("(le (load x) y)");
static Rule constIncr("(store (add (load x) y) x)");

std::map<std::string, int> RaiseToFor::stats() {
  return {
    { "raised-for-loops", raised },
  };
}

void RaiseToFor::run() {
  Base(module).run();

  Builder builder;
  auto loops = module->findAll<WhileOp>();

  for (auto loop : loops) {
    // Nothing before this loop, which means `i` cannot be initialized.
    // Doesn't seem right, but we mustn't crash in that case.
    if (loop->atFront())
      continue;

    // Analyze the condition.
    // By construction there can't be anything else in the before region.
    auto before = loop->getRegion(0);
    auto after = loop->getRegion(1);

    auto proceed = before->getLastBlock()->getLastOp();
    auto cond = proceed->DEF();
    // `stop` is the final condition (might be offseted by 1, for example)
    // `stopvar` is the original value.
    Op *ivAddr = nullptr, *stop, *stopvar;
    if (forCond.match(cond)) {
      // The induction variable is `load x`, and the address should be `x`.
      ivAddr = forCond.extract("x");
      stop = stopvar = forCond.extract("y");
    }
    if (!ivAddr && forCondLe.match(cond)) {
      ivAddr = forCondLe.extract("x");
      stopvar = forCondLe.extract("y");
      builder.setAfterOp(stopvar);
      auto one = builder.create<IntOp>({ new IntAttr(1) });
      stop = builder.create<AddIOp>({ stopvar, one });
    }
    if (!ivAddr)
      continue;

    // Make sure `stop` is loop-invariant.
    if (isa<CallOp>(stopvar) && stopvar->has<ImpureAttr>())
      continue;
    if (isa<LoadOp>(stopvar)) {
      auto addr = stopvar->DEF(0);
      bool bad = false;

      for (auto use : addr->getUses()) {
        if (isa<StoreOp>(use) && use->inside(loop)) {
          bad = true;
          break;
        }
      }
      if (bad)
        continue;
    }
    
    // Checks all stores to the address in the loop.
    bool good = true, foundIncr = false;
    Op *incr;
    for (auto use : ivAddr->getUses()) {
      if (!use->inside(loop) || isa<LoadOp>(use))
        continue;

      if (!constIncr.match(use, { { "x", ivAddr } })) {
        good = false;
        break;
      }

      Op *vi = constIncr.extract("y");
      if (!foundIncr)
        incr = vi, foundIncr = true;
      else if (incr != vi && !(isa<IntOp>(incr) && isa<IntOp>(vi) && V(incr) == V(vi))) {
        good = false;
        break;
      }

      // Check the next op: there's either none, or a continue/break.
      if (use->atBack())
        continue;
      auto next = use->nextOp();
      if (isa<ContinueOp>(next) || isa<BreakOp>(next))
        continue;

      good = false;
      break;
    }

    if (!good || !foundIncr)
      continue;
    
    // The increment doesn't need to be a constant, but it must be loop-invariant.
    if (!isa<IntOp>(incr) && incr->inside(loop)) {
      // If this is a load, we check there's no write to the same place,
      // and it's not an array.
      if (isa<LoadOp>(incr)) {
        auto addr = incr->DEF();
        if (!addr->has<BaseAttr>() || SIZE(BASE(addr)) != 4)
          continue;

        auto base = BASE(addr);
        auto stores = loop->findAll<StoreOp>();
        good = true;
        for (auto store : stores) {
          auto saddr = store->DEF(1);
          if (!saddr->has<BaseAttr>() || BASE(saddr) == base) {
            good = false;
            break;
          }
        }

        if (!good)
          continue;
      } else continue;
    }

    // We also need to check that there's a store to `ivAddr`
    // before all break/continue and the end of the after region.
    auto terms = loop->findAll<BreakOp>();
    auto conts = loop->findAll<ContinueOp>();
    std::copy(conts.begin(), conts.end(), std::back_inserter(terms));
    for (auto x : terms) {
      if (x->atFront() || !constIncr.match(x->prevOp())) {
        good = false;
        break;
      }
    }

    // TCO removes empty blocks, so this is safe.
    auto back = after->getLastBlock()->getLastOp();
    if (!good || !constIncr.match(back))
      continue;
    
    // Now time to check for initial value of the induction variable.
    Op *runner, *init;
    bool removable = true;
    for (runner = loop->prevOp(); !runner->atFront(); runner = runner->prevOp()) {
      // This checks while, for and if.
      if (runner->getRegionCount()) {
        // Find all stores inside it.
        // If ivAddr is stored inside it, we don't know the init. Give up.
        auto stores = runner->findAll<StoreOp>();
        for (auto store : stores) {
          if (store->DEF(1) == ivAddr) {
            good = false;
            break;
          }
        }
        // If the previous one is a ForOp storing to the same place,
        // then the value of `ivAddr` is exactly the stop value.
        if (isa<ForOp>(runner) && ivAddr == runner->DEF(3)) {
          init = runner->DEF(1);
          // Here `runner` will be pointing to the ForOp.
          // Cannot remove it.
          removable = false;
          break;
        }
        continue;
      }

      // The value is found here.
      if (isa<StoreOp>(runner) && runner->DEF(1) == ivAddr) {
        init = runner->DEF(0);
        break;
      }

      // The address is used between the store and the loop.
      // We can't remove that store.
      if (ivAddr->getUses().count(runner))
        removable = false;
    }

    if (!good || runner->atFront())
      continue;

    // Hoist the increment out of loop when possible.
    if (isa<IntOp>(incr) && incr->inside(loop))
      incr->moveBefore(loop);
  
    // Remove the store to induction variable as initial value, when possible.
    if (removable)
      runner->erase();

    // We've got all necessary info for `for`. (Bad phrasing.)
    builder.setBeforeOp(loop);
    // Record the `ivAddr` we use. The same alloca might be used afterwards.
    // It is not used in transformations, but is necessary for lowering.
    auto floop = builder.create<ForOp>({ init, stop, incr, ivAddr });
    auto region = floop->appendRegion();

    // Move everything in after region to the ForOp.
    const auto &bbs = after->getBlocks();
    for (auto it = bbs.begin(); it != bbs.end();) {
      auto next = it; next++;
      (*it)->moveToEnd(region);
      it = next;
    }

    // Move the before region before the ForOp, and delete the ProceedOp.
    assert(before->getBlocks().size() == 1);
    auto bb = before->getFirstBlock();
    bb->getLastOp()->erase();
    bb->inlineBefore(floop);

    std::vector<Op*> remove;
    for (auto use : ivAddr->getUses()) {
      if (!use->inside(floop))
        continue;

      // Remove all stores to ivAddr inside the loop.
      if (isa<StoreOp>(use))
        remove.push_back(use);

      // Replace all loads to it by the induction variable,
      // given by the "result" of ForOp.
      else if (isa<LoadOp>(use)) {
        use->replaceAllUsesWith(floop);
        remove.push_back(use);
      }
    }

    for (auto x : remove)
      x->erase();

    // Erase the while.
    loop->erase();

    // Record that a loop has been raised.
    raised++;
  }
}
