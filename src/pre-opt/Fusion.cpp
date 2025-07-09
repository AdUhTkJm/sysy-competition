#include "PreLoopPasses.h"

using namespace sys;

std::map<std::string, int> Fusion::stats() {
  return {
    { "fused-loops", fused },
  };
}

#define BAD(cond) if (cond) { good = false; break; }

namespace {

bool identical(Op *a, Op *b, const std::unordered_set<Op*> &written) {
  if (a == b)
    return true;

  if (a->opid != b->opid)
    return false;

  if (isa<IntOp>(a))
    return V(a) == V(b);

  if (a->getOperandCount() != b->getOperandCount())
    return false;

  for (int i = 0; i < a->getOperandCount(); i++) {
    if (!identical(a->DEF(i), b->DEF(i), written))
      return false;
  }

  if (isa<LoadOp>(a))
    return a->DEF()->has<BaseAttr>() && !written.count(BASE(a->DEF()));

  return true;
}

// We only combine for loops with identical start, stop and step for now.
// We might do peeling in the future, but I don't feel like it currently.
bool fusible(Op *a, Op *b, const std::unordered_set<Op*> &written) {
  return identical(a->DEF(0), b->DEF(0), written)
      && identical(a->DEF(1), b->DEF(1), written)
      && identical(a->DEF(2), b->DEF(2), written);
}

// Ops that can't be hoisted to before the loop.
#define PINNED(Ty) || isa<Ty>(op)
bool pinned(Op *op) {
  return (isa<CallOp>(op) && op->has<ImpureAttr>())
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
      std::unordered_set<Op*> reads, writes;

      auto calls = loop->findAll<CallOp>();
      for (auto call : calls)
        BAD(call->has<ImpureAttr>());

      auto stores = loop->findAll<StoreOp>();
      for (auto store : stores) {
        auto addr = store->DEF(1);
        BAD(!addr->has<BaseAttr>());
        writes.insert(BASE(addr));
      }

      auto loads = loop->findAll<LoadOp>();
      for (auto load : loads) {
        auto addr = load->DEF();
        BAD(!addr->has<BaseAttr>());
        reads.insert(BASE(addr));
      }
      if (!good)
        continue;

      for (next = loop->nextOp(); !next->atBack(); next = next->nextOp()) {
        // Found a fusion candidate.
        if (isa<ForOp>(next))
          break;

        BAD(pinned(next));

        // Stores and loads are hoistable when the first loop doesn't touch it.
        if (isa<StoreOp>(next)) {
          auto addr = BASE(next->DEF(1));
          BAD(reads.count(addr) || writes.count(addr));
        }
        if (isa<LoadOp>(next)) {
          auto addr = BASE(next->DEF());
          BAD(writes.count(addr));
        }
        hoisted.push_back(next);
      }
      if (!good || (next->atBack() && !isa<ForOp>(next)))
        continue;

      auto ncalls = next->findAll<CallOp>();
      for (auto call : ncalls)
        BAD(call->has<ImpureAttr>());

      // Find all writes in `next`.
      auto nwrites = next->findAll<StoreOp>();
      for (auto op : nwrites) {
        auto addr = op->DEF(1);
        BAD(!addr->has<BaseAttr>());
        writes.insert(BASE(addr));
      }

      // Two consecutive for's. Check whether they can get combined.
      if (!good || !fusible(next, loop, writes))
        return;

      // A very strict way of checking dependency:
      // 1) all subscripts must be the same (except for those that are only read);
      // 2) non-array variables must be disjoint.
      // Polyhedral approach here? Might extend when I have time.
      std::unordered_set<Op*> arrays;
      auto func = loop->getParentOp<FuncOp>();
      auto first = func->getRegion()->getFirstBlock();
      for (auto op = first->getFirstOp(); isa<AllocaOp>(op) && !op->atBack(); op = op->nextOp()) {
        if (SIZE(op) > 4)
          arrays.insert(op);
      }
      // The previous loop didn't deal with the last op.
      auto final = first->getLastOp();
      if (isa<AllocaOp>(final) && SIZE(final) > 4)
        arrays.insert(final);

      AffineExpr subscripts;
      Op *subscrOp = nullptr;
      std::unordered_set<Op*> nonsub, hassub;

      auto region = next->getRegion();
      auto bb = region->getFirstBlock();

      // Collect all stores and loads in both loops.
      std::vector<Op*> access(stores.begin(), stores.end());
      std::copy(loads.begin(), loads.end(), std::back_inserter(access));

      auto naccess = next->findAll<LoadOp>();
      std::copy(nwrites.begin(), nwrites.end(), std::back_inserter(naccess));

      std::unordered_set<Op*> local, nlocal;
      for (auto op : naccess) {
        auto addr = isa<LoadOp>(op) ? op->DEF() : op->DEF(1);
        if (!arrays.count(addr))
          nlocal.insert(addr);
      }
      for (auto op : access) {
        auto addr = isa<LoadOp>(op) ? op->DEF() : op->DEF(1);
        if (!arrays.count(addr))
          local.insert(addr);
      }

      // Check disjointness.
      for (auto op : nlocal)
        BAD(local.count(op));

      std::copy(naccess.begin(), naccess.end(), std::back_inserter(access));

      // Check subscript.
      for (auto op : access) {
        auto addr = isa<LoadOp>(op) ? op->DEF() : op->DEF(1);
        auto base = BASE(addr);
        if (!writes.count(base))
          continue;

        if (!addr->has<SubscriptAttr>()) {
          BAD(hassub.count(base));
          nonsub.insert(base);
          if (arrays.count(base) || isa<GetGlobalOp>(base)) {
            if (!subscrOp)
              subscrOp = addr;
            BAD(!identical(subscrOp, addr, writes));
          }
          continue;
        }

        BAD(nonsub.count(base));
        hassub.insert(base);

        const auto &subscript = SUBSCRIPT(addr);
        if (subscripts.empty())
          subscripts = subscript;

        BAD(subscripts != subscript);
      }

      if (!good)
        continue;

      // In case the induction variable aren't the same,
      // we add another store at the end of the second loop.
      if (loop->DEF(3) != next->DEF(3)) {
        Builder builder;
        builder.setAfterOp(next);
        Value stop = next->DEF(1);
        builder.create<StoreOp>({ stop, next->DEF(3) }, { new SizeAttr(4) });
      }

      // Hoist the ops between `next` and `loop` to before `loop`.
      for (auto op : hoisted)
        op->moveBefore(loop);

      // Move all ops in `next` to `loop`.
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
