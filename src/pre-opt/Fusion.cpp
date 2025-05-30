#include "PreLoopPasses.h"

using namespace sys;

std::map<std::string, int> Fusion::stats() {
  return {
    { "fused-loops", fused },
  };
}

namespace {

bool identical(Op *a, Op *b) {
  if (a->opid != b->opid)
    return false;

  if (isa<IntOp>(a))
    return V(a) == V(b);

  return a == b;
}

// We only combine for loops with identical start, stop and step for now.
// We might do peeling in the future, but I don't feel like it currently.
bool fusible(Op *a, Op *b) {
  return identical(a->DEF(0), b->DEF(0))
      && identical(a->DEF(1), b->DEF(1))
      && identical(a->DEF(2), b->DEF(2))
      && identical(a->DEF(3), b->DEF(3));
}

// Ops that can't be hoisted to before the loop.
#define PINNED(Ty) || isa<Ty>(op)
bool pinned(Op *op) {
  return (isa<CallOp>(op) && op->has<ImpureAttr>())
    PINNED(LoadOp)
    PINNED(StoreOp)
    PINNED(IfOp)
    PINNED(WhileOp)
    PINNED(ForOp)
    PINNED(BreakOp)
    PINNED(ContinueOp)
    PINNED(ProceedOp)
    PINNED(ReturnOp);
}

}

void Fusion::runImpl(FuncOp *func) {
  bool changed;
  do {
    changed = false;
    auto loops = func->findAll<ForOp>();

    for (auto loop : loops) {
      if (loop->atBack())
        continue;

      std::vector<Op*> hoisted;
      Op *next;
      bool good = true;
      for (next = loop->nextOp(); !next->atBack(); next = next->nextOp()) {
        // Found a fusion candidate.
        if (isa<ForOp>(next))
          break;

        if (pinned(next)) {
          good = false;
          break;
        }
        hoisted.push_back(next);
      }
      if (!good || (next->atBack() && !isa<ForOp>(next)))
        continue;

      // Two consecutive for's. Check whether they can get combined.
      if (!fusible(next, loop))
        return;

      // TODO: Check dependency.

      // Hoist the ops between `next` and `loop` to before `loop`.
      for (auto op : hoisted)
        op->moveBefore(loop);

      // Move all ops in `next` to `loop`.
      auto region = next->getRegion();
      auto bb = region->getFirstBlock();
      bb->inlineToEnd(loop->getRegion()->getLastBlock());

      // The ops might still be referencing the induction variable of `next`.
      // Change them to refer to `loop` instead.
      next->replaceAllUsesWith(loop);

      // Erase the now-empty `next`.
      next->erase();
      
      fused++;
      changed = true;
      break;
    }
  } while (changed);
}

void Fusion::run() {
  auto funcs = collectFuncs();

  for (auto func : funcs)
    runImpl(func);
}
