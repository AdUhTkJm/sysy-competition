#include "Passes.h"

using namespace sys;

// TODO
bool getIncrement(Op *store, int &inc) {
  auto value = store->getOperand(0).defining;
  auto addr = store->getOperand(1).defining;
  return false;
}

void RecognizeFor::run() {
  auto loops = module->findAll<WhileOp>();

  // First, identify induction variable of each loop.
  // As this runs before FlattenCFG, an induction variable:
  //   1) must be stored as the very last instruction of the `after` region;
  //   2) must be stored just before every `continue`;
  //   3) must not be stored elsewhere;
  //   4) the updated value must be Add ((Load IndVar), Const), and the const is the same everywhere.
  //
  // Note that we don't need to check `return`s and `break`s.
  //
  // These conditions are more restrictive than LLVM SCEV,
  // but they suffice.

  for (auto loop : loops) {
    auto region = loop->getRegion();
    auto endOp = region->getLastBlock()->getLastOp();
    if (!isa<StoreOp>(endOp))
      continue;

    int inc;
    auto addr = endOp->getOperand(1).defining;
    if (!getIncrement(endOp, inc))
      continue;

    
    std::set<Op*> stores { endOp };

    auto conts = loop->findAll<ContinueOp>();
    bool good = true;
    for (auto cont : conts) {
      if (cont == cont->getParent()->getFirstOp() || !isa<StoreOp>(cont->prevOp())) {
        good = false;
        break;
      }

      auto prev = cont->prevOp();
      auto prevAddr = endOp->getOperand(1).defining;
      int prevInc;
      if (prevAddr != addr || !getIncrement(prev, prevInc) || prevInc != inc) {
        good = false;
        break;
      }

      stores.insert(prev);
    }

    if (!good)
      continue;

    // Ensure the stores collected above are the only stores in the WhileOp.
    auto allStores = loop->findAll<StoreOp>();
    for (auto store : allStores) {
      if (!stores.count(store)) {
        good = false;
        break;
      }
    }

    if (!good)
      continue;

    // All checks have passed. Now time to convert it into ForOp.
    // The semantics of for:
    //    %1 = for %2 %3 { /* Region */ }
    //
    // %1 is the induction variable;
    // %2 is the lower bound;
    // %3 is the upper bound.
    // TODO
  }
}
