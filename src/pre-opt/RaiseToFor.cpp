#include "PreLoopPasses.h"
#include "../utils/Matcher.h"

using namespace sys;

static Rule forCond("(lt (load x) y)");
static Rule constIncr("(add (load x) 'a)");
static Rule store("(store (add (load x) 'a) x)");

void RaiseToFor::run() {
  auto loops = module->findAll<WhileOp>();

  for (auto loop : loops) {
    // Analyze the condition.
    auto before = loop->getRegion(0);
    auto after = loop->getRegion(1);

    auto proceed = before->getLastBlock()->getLastOp();
    auto cond = proceed->DEF();
    if (!forCond.match(cond))
      continue;

    // The induction variable is `load x`, and the address should be `x`.
    auto ivAddr = forCond.extract("x");
    
    // Checks all stores to the address in the loop.
    bool good = true, foundIncr = false;
    int incr;
    for (auto use : ivAddr->getUses()) {
      if (!use->inside(loop))
        continue;

      if (!constIncr.match(use, { { "x", ivAddr } })) {
        good = false;
        break;
      }

      Op *vi = constIncr.extract("'a");
      // RegularFold guarantees `V(vi) != 0`.
      if (!foundIncr)
        incr = V(vi), foundIncr = true;
      else if (incr != V(vi)) {
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

    if (!good)
      continue;

    // We also need to check that there's a store to `ivAddr`
    // before all break/continue and the end of the after region.
    auto terms = loop->findAll<BreakOp>();
    auto conts = loop->findAll<ContinueOp>();
    std::copy(conts.begin(), conts.end(), std::back_inserter(terms));
    for (auto x : terms) {
      if (x->atFront() || !store.match(x->prevOp())) {
        good = false;
        break;
      }
    }

    // TCO removes empty blocks, so this is safe.
    auto back = after->getLastBlock()->getLastOp();
    if (!good || !store.match(back))
      continue;
    
    // Now time to check for initial value of the induction variable.
    
  }
}
