#include "Passes.h"

using namespace sys;

void EarlyConstFold::run() {
  auto allocas = module->findAll<AllocaOp>();

  for (auto alloca : allocas) {
    auto uses = alloca->getUses();
    Op *store = nullptr;
    bool good = true;
    for (auto use : uses) {
      if (isa<LoadOp>(use))
        continue;
      
      if (isa<StoreOp>(use)) {
        if (store) {
          good = false;
          break;
        }
        store = use;
        continue;
      }

      // This alloca is an array.
      good = false;
      break;
    }

    if (!good || !store)
      continue;

    // Now this is a constant value.
    Op *def = store->getOperand(0).defining;

    // We only propagate compiler-time constants.
    // Other things are better done in Mem2Reg.
    if (!isa<IntOp>(def))
      continue;
    
    for (auto use : uses) {
      if (isa<LoadOp>(use)) {
        use->replaceAllUsesWith(def);
        use->erase();
      }
    }

    store->erase();
    alloca->erase();
  }
}
