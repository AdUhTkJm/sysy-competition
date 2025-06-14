#include "CleanupPasses.h"

using namespace sys;

std::map<std::string, int> RedundancyElim::stats() {
  return {
    { "removed-redundancy", elim },
  };
}

namespace {

#define PINNED(Ty) || isa<Ty>(op)
bool pinned(Op *op) {
  return (isa<CallOp>(op) && op->has<ImpureAttr>())
    PINNED(StoreOp)
    PINNED(LoadOp)
    PINNED(PhiOp)
    PINNED(GotoOp)
    PINNED(BranchOp);
}

bool identical(Op *a, Op *b) {
  if (a->opid != b->opid || pinned(a))
    return false;
  if (isa<IntOp>(a))
    return V(a) == V(b);
  if (isa<FloatOp>(a))
    return F(a) == F(b);

  if (a->getOperandCount() != b->getOperandCount())
    return false;

  for (int i = 0; i < a->getOperandCount(); i++) {
    if (a->DEF(i) != b->DEF(i))
      return false;
  }
  return true;
}

}

bool RedundancyElim::runImpl(Region *region) {
  region->updateDoms();
  bool changed = false;
  for (auto bb : region->getBlocks()) {
    auto term = bb->getLastOp();
    if (!isa<BranchOp>(term))
      continue;

    // We only hoist from an `if`, rather than while.
    // So both bb1 and bb2 should have a single predecessor.
    auto bb1 = TARGET(term), bb2 = ELSE(term);
    if (bb1->preds.size() != 1 || bb2->preds.size() != 1)
      continue;

    // Check whether there's an completely identical op in both arms.
    // For performance reasons, don't continue if there's too much ops to examine.
    if (bb1->getOpCount() * bb2->getOpCount() >= 10000)
      continue;

    for (auto op : bb1->getOps()) {
      for (auto x : bb2->getOps()) {
        if (identical(op, x)) {
          // We're going to move to the end of `term`,
          // so must make sure every operand dominates `bb`.
          bool good = true;
          for (auto operand : op->getOperands()) {
            if (!operand.defining->getParent()->dominates(bb)) {
              good = false;
              break;
            }
          }
          if (!good)
            break;

          op->moveBefore(term);
          x->replaceAllUsesWith(op);
          x->erase();
          changed = true;
          goto out;
        }
      }
    }
    out:;
  }
  return changed;
}

void RedundancyElim::run() {
  auto funcs = collectFuncs();

  for (auto func : funcs) {
    while (runImpl(func->getRegion()));
  }
}