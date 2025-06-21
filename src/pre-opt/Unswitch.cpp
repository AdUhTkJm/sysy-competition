#include "PreLoopPasses.h"
#include "../utils/Matcher.h"

using namespace sys;

std::map<std::string, int> Unswitch::stats() {
  return {
    { "unswitched-loops", unswitched },
  };
}

namespace {

Rule cmpmod("(eq (mod x 'a) 'b)");
Rule cmpmod0("(mod x 'a)");
Rule addmod("(mod (add x 'a) 'b)");
Rule cmplt("(lt x y)");
Rule cmpgt("(lt y x)");

// It's guaranteed that the induction variable will be a multiple of `vi`
// at the beginning of the loop.
void tidymod(Op *loop, int vi) {
  int start = V(loop->DEF(0));
  auto region = loop->getRegion();
  auto entry = region->getFirstBlock();
  Builder builder;

  auto ops = entry->getOps();
  for (auto op : ops) {
    if (!isa<ModIOp>(op))
      continue;

    // This is a direct mod. See `start` to check.
    if (op->DEF(0) == loop && isa<IntOp>(op->DEF(1))) {
      auto v = V(op->DEF(1));
      if (vi % v)
        continue;
      builder.replace<IntOp>(op, { new IntAttr(start % v) });
    }

    if (!addmod.match(op, { { "x", loop } }))
      continue;

    auto incr = V(addmod.extract("'a"));
    auto mod = V(addmod.extract("'b"));

    if (vi % mod)
      continue;
    builder.replace<IntOp>(op, { new IntAttr((start + incr) % mod) });
  }
}

}

void unroll(Op *loop, int vi) {
  auto region = loop->getRegion();
  auto entry = region->getFirstBlock();

  Builder builder;
  std::list<Op*> body = entry->getOps();
  std::unordered_map<Op*, Op*> opmap;

  const std::function<void (Op*)> copy = [&](Op *x) {
    auto copied = builder.copy(x);
    opmap[x] = copied;

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

  // Copy the loop body.
  for (int z = 1; z < vi; z++) {
    opmap.clear();
    builder.setToBlockEnd(entry);

    // Now the induction variable should be `i + z`.
    auto zi = builder.create<IntOp>({ new IntAttr(z) });
    auto incr = builder.create<AddIOp>({ loop, zi });
    opmap[loop] = incr;

    for (auto x : body)
      copy(x);

    // Rewire operands.
    for (auto [_, v] : opmap) {
      for (int i = 0; i < v->getOperandCount(); i++) {
        auto def = v->DEF(i);
        v->setOperand(i, opmap.count(def) ? opmap[def] : def);
      }
    }
    // This also changes `incr` itself. Restore it.
    incr->setOperand(0, loop);
  }

  // Now the step is `vi` times larger.
  auto stop = loop->DEF(1);
  auto step = loop->DEF(2);
  auto start = loop->DEF(0);

  builder.setBeforeOp(loop);
  auto xstep = builder.create<IntOp>({ new IntAttr(V(step) * vi)});
  loop->setOperand(2, xstep);

  // Loop end should be truncated.
  // The way to do this is to first calculate `cnt = (stop - start + step - 1) / step`:
  Value sub = builder.create<SubIOp>({ stop, start->getResult() });
  Value v = builder.create<IntOp>({ new IntAttr(V(step) - 1) });
  Value plus = builder.create<AddIOp>({ sub, v });
  Value cnt = builder.create<DivIOp>({ plus, step });

  // The number of unrolled iterations is `cnt / n`:
  Value n = builder.create<IntOp>({ new IntAttr(vi) });
  Value unrolled = builder.create<DivIOp>({ plus, n });

  // Then the ending point is exactly `end = start + unrolled * n * step`.
  Value mul2 = builder.create<MulIOp>({ unrolled, n });
  Value mul = builder.create<MulIOp>({ mul2, step });
  Value end = builder.create<AddIOp>({ start, mul });
  loop->setOperand(1, end);

  // Create a side-loop from `end` to `stop`.
  builder.setAfterOp(loop);
  auto ivAddr = loop->DEF(3);
  auto sideloop = builder.create<ForOp>({ end, stop, step, ivAddr });
  auto sregion = sideloop->appendRegion();
  auto sEntry = sregion->appendBlock();

  // Copy the body to the sideloop.
  builder.setToBlockEnd(sEntry);
  opmap.clear();
  // Rename induction variable.
  opmap[loop] = sideloop;

  for (auto x : body)
    copy(x);

  // Rewire operands.
  for (auto [_, v] : opmap) {
    for (int i = 0; i < v->getOperandCount(); i++) {
      auto def = v->DEF(i);
      v->setOperand(i, opmap.count(def) ? opmap[def] : def);
    }
  }
}

bool Unswitch::cmpmod(Op *loop, Op *cond) {
  int vi;
  if (!::cmpmod.match(cond, { { "x", loop } })) {
    if (!cmpmod0.match(cond, { { "x", loop } }))
      return false;
    // This means (x % 'a) != 0.
    // The procedure also works.
    vi = V(cmpmod0.extract("'a"));
  } else
    vi = V(::cmpmod.extract("'a"));

  vi = std::abs(vi);
  if (vi > 16 || vi <= 1)
    return false;

  // If `start` is not a constant,
  // we wouldn't be able to pre-calculate the mod value.
  if (!isa<IntOp>(loop->DEF(0)))
    return false;

  // Unroll the loop `vi` times.
  unroll(loop, vi);
  // Check `mod`s inside the loop and erase them when possible.
  tidymod(loop, vi);
  unswitched++;
  return true;
}

bool Unswitch::ltconst(Op *loop, Op *cond) {
  if (!cmplt.match(cond, { { "x", loop } }))
    return false;
  
  auto limit = cmplt.extract("y");
  // Check whether it's a constant.
  if (limit->inside(loop) && !isa<IntOp>(limit))
    return false;

  if (isa<LoadOp>(limit)) {
    if (!limit->has<BaseAttr>())
      return false;

    // Check if there's any store to this limit.
    auto stores = loop->findAll<StoreOp>();
    for (auto store : stores) {
      if (!store->has<BaseAttr>() || BASE(store) == limit)
        return false;
    }
  }

  if (isa<IntOp>(limit) && limit->inside(loop))
    limit->moveBefore(loop);

  // Now it's safe. Split the loop into two.

  // Start from the original start to the new limit.
  auto start = loop->getOperand(0);
  auto stop = loop->getOperand(1);
  auto step = loop->getOperand(2);
  auto ivAddr = loop->getOperand(3);
  // It's always save to do so. We've verified that the step is loop-invariant.
  if (step.defining->inside(loop))
    step.defining->moveBefore(loop);
  
  Builder builder;
  builder.setBeforeOp(loop);

  // Create a min of `limit` and `stop`.
  // min = (stop < limit) ? stop : limit
  auto lt = builder.create<LtOp>({ stop, limit });
  auto min = builder.create<SelectOp>({ lt, stop, limit });

  auto floop = builder.create<ForOp>({ start, min, step, ivAddr });

  // Calculate the starting point of the `i >= limit` branch.
  // The value should be equal to:
  //   start + (limit - start + step - 1) / step * step
  builder.setBeforeOp(loop);
  Value sub = builder.create<SubIOp>({ limit, start });
  Value one = builder.create<IntOp>({ new IntAttr(1) });
  Value sub2 = builder.create<SubIOp>({ step, one });
  Value plus = builder.create<AddLOp>({ sub, sub2 });
  Value div = builder.create<DivLOp>({ plus, step });
  Value mul = builder.create<MulLOp>({ div, step });
  // This might overflow. Saturation is needed.
  Value lim = builder.create<AddLOp>({ start, mul });
  Value imax = builder.create<IntOp>({ new IntAttr(2147483647) });
  Value _lt = builder.create<LtOp>({ lim, imax });
  Value x = builder.create<SelectOp>({ _lt, lim, imax });
  // Take the maximum of `x` and the original `start`.
  Value gt = builder.create<LtOp>({ start, x });
  Value y = builder.create<SelectOp>({ gt, x, start });
  loop->setOperand(0, y);

  // Copy the operations into that new loop.
  auto region = loop->getRegion();
  auto entry = region->getFirstBlock();

  std::list<Op*> body = entry->getOps();
  std::unordered_map<Op*, Op*> opmap;

  const std::function<void (Op*)> copy = [&](Op *x) {
    auto copied = builder.copy(x);
    opmap[x] = copied;

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

  auto fregion = floop->appendRegion();
  auto fentry = fregion->appendBlock();
  builder.setToBlockEnd(fentry);
  opmap[loop] = floop;

  for (auto x : body)
    copy(x);

  // Rewire operands.
  for (auto [_, v] : opmap) {
    for (int i = 0; i < v->getOperandCount(); i++) {
      auto def = v->DEF(i);
      v->setOperand(i, opmap.count(def) ? opmap[def] : def);
    }
  }

  // Look for the if for the newly created loop.
  auto fcond = opmap[cond];
  const auto &fuses = fcond->getUses();
  for (auto use : fuses) {
    if (!isa<IfOp>(use))
      continue;

    // Move all ops in the "true" region outside the if.
    auto ifso = use->getRegion(0);
    ifso->getFirstBlock()->inlineBefore(use);
  }
  // Remove all if's.
  for (auto it = fuses.begin(); it != fuses.end();) {
    auto next = it; next++;
    if (isa<IfOp>(*it))
      (*it)->erase();
    it = next;
  }

  // Similarly do it for the other side.
  const auto &uses = cond->getUses();
  for (auto use : uses) {
    if (!isa<IfOp>(use))
      continue;

    // Move all ops in the "false" region outside the if,
    // only if that region is present.
    if (use->getRegionCount() > 1) {
      auto ifnot = use->getRegion(1);
      ifnot->getFirstBlock()->inlineBefore(use);
    }
  }
  // Remove all if's.
  for (auto it = uses.begin(); it != uses.end();) {
    auto next = it; next++;
    if (isa<IfOp>(*it))
      (*it)->erase();
    it = next;
  }

  unswitched++;
  return true;
}

// Basically a duplicate of the previous one.
bool Unswitch::gtconst(Op *loop, Op *cond) {
  if (!cmpgt.match(cond, { { "x", loop } }))
    return false;
  
  auto limit = cmpgt.extract("y");
  // Check whether it's a constant.
  if (limit->inside(loop) && !isa<IntOp>(limit))
    return false;

  if (isa<LoadOp>(limit)) {
    if (!limit->has<BaseAttr>())
      return false;

    // Check if there's any store to this limit.
    auto stores = loop->findAll<StoreOp>();
    for (auto store : stores) {
      if (!store->has<BaseAttr>() || BASE(store) == limit)
        return false;
    }
  }

  if (isa<IntOp>(limit) && limit->inside(loop))
    limit->moveBefore(loop);

  // Now it's safe. Split the loop into two.

  // Start from the original start to the new limit.
  auto start = loop->getOperand(0);
  auto stop = loop->getOperand(1);
  auto step = loop->getOperand(2);
  auto ivAddr = loop->getOperand(3);
  // It's always save to do so. We've verified that the step is loop-invariant.
  if (step.defining->inside(loop))
    step.defining->moveBefore(loop);
  
  Builder builder;
  builder.setBeforeOp(loop);

  // Create a min of `limit` and `stop`.
  // One difference from the previous one is that we're off by one.
  // Or rather, we'd use `limit := limit + 1` since we want `i >= limit`.
  Value one = builder.create<IntOp>({ new IntAttr(1) });
  limit = builder.create<AddIOp>({ limit, one });

  // min = (stop < limit) ? stop : limit
  auto lt = builder.create<LtOp>({ stop, limit });
  auto min = builder.create<SelectOp>({ lt, stop, limit });

  auto floop = builder.create<ForOp>({ start, min, step, ivAddr });

  // Calculate the starting point of the `i >= limit` branch.
  // The value should be equal to:
  //   start + (limit - start + step - 1) / step * step
  builder.setBeforeOp(loop);
  Value sub = builder.create<SubIOp>({ limit, start });
  Value sub2 = builder.create<SubIOp>({ step, one });
  Value plus = builder.create<AddLOp>({ sub, sub2 });
  Value div = builder.create<DivLOp>({ plus, step });
  Value mul = builder.create<MulLOp>({ div, step });
  // This might overflow. Saturation is needed.
  Value lim = builder.create<AddLOp>({ start, mul });
  Value imax = builder.create<IntOp>({ new IntAttr(2147483647) });
  Value _lt = builder.create<LtOp>({ lim, imax });
  Value x = builder.create<SelectOp>({ _lt, lim, imax });
  // Take the maximum of `x` and the original `start`.
  Value gt = builder.create<LtOp>({ start, x });
  Value y = builder.create<SelectOp>({ gt, x, start });
  loop->setOperand(0, y);

  // Copy the operations into that new loop.
  auto region = loop->getRegion();
  auto entry = region->getFirstBlock();

  std::list<Op*> body = entry->getOps();
  std::unordered_map<Op*, Op*> opmap;

  const std::function<void (Op*)> copy = [&](Op *x) {
    auto copied = builder.copy(x);
    opmap[x] = copied;

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

  auto fregion = floop->appendRegion();
  auto fentry = fregion->appendBlock();
  builder.setToBlockEnd(fentry);
  opmap[loop] = floop;

  for (auto x : body)
    copy(x);

  // Rewire operands.
  for (auto [_, v] : opmap) {
    for (int i = 0; i < v->getOperandCount(); i++) {
      auto def = v->DEF(i);
      v->setOperand(i, opmap.count(def) ? opmap[def] : def);
    }
  }

  // Look for the if for the newly created loop.
  auto fcond = opmap[cond];
  const auto &fuses = fcond->getUses();
  for (auto use : fuses) {
    if (!isa<IfOp>(use))
      continue;

    // Move all ops in the "false" region outside the if,
    // only if that region is present.
    if (use->getRegionCount() > 1) {
      auto ifnot = use->getRegion(1);
      ifnot->getFirstBlock()->inlineBefore(use);
    }
  }
  // Remove all if's.
  for (auto it = fuses.begin(); it != fuses.end();) {
    auto next = it; next++;
    if (isa<IfOp>(*it))
      (*it)->erase();
    it = next;
  }

  // Similarly do it for the other side.
  const auto &uses = cond->getUses();
  for (auto use : uses) {
    if (!isa<IfOp>(use))
      continue;

    // Move all ops in the "true" region outside the if.
    auto ifnot = use->getRegion(0);
    ifnot->getFirstBlock()->inlineBefore(use);
  }
  // Remove all if's.
  for (auto it = uses.begin(); it != uses.end();) {
    auto next = it; next++;
    if (isa<IfOp>(*it))
      (*it)->erase();
    it = next;
  }

  unswitched++;
  return true;
}

bool Unswitch::runImpl(Op *loop) {
  // Erased loop, but yet to be deleted.
  if (!loop->getOperandCount())
    return false;

  // The step must be a constant.
  auto step = loop->DEF(2);

  // Find an "if" that is related to the induction variable.
  Op *branch = nullptr;
  auto region = loop->getRegion();
  auto entry = region->getFirstBlock();
  for (auto op : entry->getOps()) {
    if (isa<IfOp>(op)) {
      branch = op;
      break;
    }
  }
  if (!branch)
    return false;

  auto cond = branch->DEF();

  return 
    (isa<IntOp>(step) && cmpmod(loop, cond)) ||
    ltconst(loop, cond) ||
    gtconst(loop, cond);
}

void Unswitch::run() {
  bool changed;
  do {
    changed = false;
    auto loops = module->findAll<ForOp>();
    for (auto loop : loops)
      changed |= runImpl(loop);
  } while (changed);
}
