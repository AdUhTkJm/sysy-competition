#include "LoopPasses.h"
#include "CleanupPasses.h"
#include "Passes.h"
#include "../utils/Matcher.h"

using namespace sys;

std::map<std::string, int> SCEV::stats() {
  return {
    { "expanded", expanded }
  };
}

namespace {

Rule constIncr("(add x 'a)");
Rule constIncrL("(addl x 'a)");
Rule modIncr("(mod (add x y) 'a)");
Rule constDiv("(div x 'a)");
Rule invarAdd("(addl x y)");

}

void SCEV::rewrite(BasicBlock *bb, LoopInfo *info) {
  auto preheader = info->preheader;
  auto header = info->header;
  auto latch = info->getLatch();
  // Maps an op to its increase amount, when the amount is non-constant but loop-invariant.
  // The increase amount is actually a multiplication of all ops in the vector.
  std::unordered_map<Op*, std::vector<Op*>> invar;
  Builder builder;

  for (auto op : bb->getOps()) {
    if (op->has<IncreaseAttr>())
      continue;

    if (isa<AddIOp>(op) || isa<AddLOp>(op)) {
      auto x = op->DEF(0);
      auto y = op->DEF(1);
      if (!x->has<IncreaseAttr>()) {
        if (y->has<IncreaseAttr>())
          std::swap(x, y);
        else {
          // When neither has IncreaseAttr, it's still possible that `x` or `y` is `invar`.
          // It can't proceed when both or none of them are there.
          // TODO: make up an addition at preheader when both are `invar`?

          if (!(invar.count(x) ^ invar.count(y)))
            continue;
          if (!invar.count(x) && invar.count(y))
            std::swap(x, y);
          if (info->contains(y->getParent()) && !isa<IntOp>(y))
            continue;

          invar[op] = invar[x];
          start[y] = y;
          continue;
        }
      }

      // Case 1. x + <invariant>
      if (y->getParent()->dominates(preheader)) {
        start[y] = y;
        op->add<IncreaseAttr>(INCR(x)->amt);
        continue;
      }

      // Case 2. x + y, while y has a increase attr
      if (auto incr = y->find<IncreaseAttr>()) {
        auto amt = INCR(x)->amt;
        auto amt2 = incr->amt;
        if (amt.size() < amt2.size())
          std::swap(amt, amt2);

        // Add offsets.
        for (int i = 0; i < amt2.size(); i++)
          amt[i] += amt2[i];
        op->add<IncreaseAttr>(amt);
        continue;
      }
    }

    if (isa<SubIOp>(op)) {
      auto x = op->DEF(0);
      auto y = op->DEF(1);
      if (!x->has<IncreaseAttr>()) {
        if (y->has<IncreaseAttr>())
          std::swap(x, y);
        else
          continue;
      }

      // Case 1. x - <invariant>
      if (y->getParent()->dominates(preheader)) {
        start[y] = y;
        op->add<IncreaseAttr>(INCR(x)->amt);
        continue;
      }
    }

    if (isa<MulIOp>(op)) {
      auto x = op->DEF(0);
      auto y = op->DEF(1);
      // Case 1. x * <invar>
      if (x->has<IncreaseAttr>() && INCR(x)->amt.size() == 1 && !info->contains(y->getParent())) {
        auto amt = INCR(x)->amt[0];
        std::vector<Op*> incr = { y };
        if (amt != 1) {
          builder.setBeforeOp(info->preheader->getLastOp());
          auto vi = builder.create<IntOp>({ new IntAttr(amt) });
          incr.push_back(vi);
          start[vi] = vi;
        }
        invar[op] = incr;
        start[y] = y;
        continue;
      }

      // Case 2. <invar> * <invar>
      if (invar.count(x) && !info->contains(y->getParent())) {
        invar[op] = invar[x];
        invar[op].push_back(y);
        start[y] = y;
        continue;
      }

      if (!x->has<IncreaseAttr>()) {
        if (y->has<IncreaseAttr>())
          std::swap(x, y);
        else
          continue;
      }

      // Case 3. x * 'a
      if (isa<IntOp>(y)) {
        auto amt = INCR(x)->amt;
        int v = V(y);
        for (auto &x : amt)
          x *= v;
        op->add<IncreaseAttr>(amt);
        start[y] = y;
        continue;
      }
    }
  }

  // After marking, now time to rewrite.
  auto term = preheader->getLastOp();
  builder.setBeforeOp(term);

  // for (auto [k, v] : invar) {
  //   std::cerr << k;
  //   for (auto x : v)
  //     std::cerr << "  " << x;
  // }

  std::vector<Op*> candidates;
  for (auto op : bb->getOps()) {
    if (isa<PhiOp>(op) || (!invar.count(op) && !op->has<IncreaseAttr>()))
      continue;

    if (nochange.count(op))
      continue;

    candidates.push_back(op);
  }

  // Const-unroll might introduce a lot of new increasing things,
  // if the unrolled loop have a surrounding loop.
  // We don't want to assign a phi to each of them.
  // We use a threshold to guard against this.
  int sz = 0;
  for (auto c : candidates) {
    if (isa<AddLOp>(c))
      sz++;
  }
  if (sz >= 4) {
    for (auto op : candidates)
      nochange.insert(op);
    candidates.clear();
  }

  // Try to find all operands in `start`.
  // Then create a `clone` which would be the starting value.
  std::vector<Op*> produced;

  for (auto op : candidates) {
    bool good = true;
    for (auto operand : op->getOperands()) {
      auto def = operand.defining;

      if (!start.count(def)) {
        nochange.insert(op);
        good = false;
        break;
      }
    }
    // This is probably the (i + 1) in `i = i + 1`.
    // We cannot rewrite it.
    if (!good)
      continue;

    auto clone = builder.copy(op);
    clone->removeAllOperands();
    for (auto operand : op->getOperands()) {
      auto def = operand.defining;
      clone->pushOperand(start[def]);
    }
    start[op] = clone;
    produced.push_back(op);
  }

  // Now replace them with the phi, create an `add` and a new phi at header.
  // We only want to replace memory accesses.
  for (auto op : produced) {
    if (!isa<AddLOp>(op))
      continue;

    builder.setToBlockStart(header);
    auto phi = builder.create<PhiOp>({ start[op] }, { new FromAttr(preheader) });
    builder.setBeforeOp(op);

    Op *add;
    if (invar.count(op)) {
      const auto &amt = invar[op];
      auto incr = amt[0];
      for (int i = 1; i < amt.size(); i++)
        incr = builder.create<MulIOp>({ incr->getResult(), amt[i] });
      add = builder.create<AddLOp>({ phi, incr });
    } else {
      auto amt = INCR(op)->amt;
      if (amt.size() > 1)
        continue;

      auto vi = builder.create<IntOp>({ new IntAttr(amt[0]) });
      add = builder.create<AddLOp>({ phi, vi });
    }

    op->replaceAllUsesWith(phi);
    op->erase();

    phi->pushOperand(add);
    phi->add<FromAttr>(latch);
  }
  
  expanded += produced.size();
  
  for (auto child : domtree[bb]) {
    if (info->contains(child))
      rewrite(child, info);
  }
}

void SCEV::runImpl(LoopInfo *info) {
  for (auto loop : info->subloops)
    runImpl(loop);

  if (info->latches.size() > 1)
    return;

  auto header = info->header;
  auto latch = info->getLatch();
  // Check rotated loops.
  if (!isa<BranchOp>(latch->getLastOp()))
    return;
  if (isa<BranchOp>(header->getLastOp()) && header != latch)
    return;

  auto phis = header->getPhis();
  auto preheader = info->preheader;
  if (!preheader)
    return;

  if (info->exits.size() != 1)
    return;

  // Inspect phis to find the amount by which something increases.
  start.clear();
  std::unordered_set<Op*> mods;
  std::set<std::pair<Op*, int>> divs;
  Builder builder;
  for (auto phi : phis) {
    auto latchval = Op::getPhiFrom(phi, latch);
    // Try to match (add x 'a).
    if (constIncr.match(latchval, { { "x", phi } })) {
      auto v = constIncr.extract("'a");
      phi->add<IncreaseAttr>(V(v));

      // Also find out the start value.
      start[phi] = Op::getPhiFrom(phi, preheader);
      // If the latchval is used more than once (elsewhere than the phi),
      // We also record it's startval: phi + step.
      if (latchval->getUses().size() > 1) {
        builder.setAfterOp(start[phi]);
        auto vi = builder.create<IntOp>({ new IntAttr(V(v)) });
        auto add = builder.create<AddIOp>({ start[phi], vi });
        start[latchval] = add;
      }
    }

    // Also try to match repeated modulus.
    if (modIncr.match(latchval, { { "x", phi } })) {
      // Check that the phi is never referred to elsewhere
      // (otherwise the transform wouldn't be sound).
      int usecnt = 0;
      for (auto use : phi->getUses()) {
        if (info->contains(use->getParent()))
          usecnt++;
      }
      // The only use is the increment.
      if (usecnt == 1)
        mods.insert(phi);
    }
  }

  // See if this phi comes from `x + <IncreaseAttr { a }>,
  // If so, this is <IncreaseAttr { 0, a }>.
  for (auto phi : phis) {
    auto latchval = Op::getPhiFrom(phi, latch);
    if (isa<AddIOp>(latchval) || isa<AddLOp>(latchval)) {
      auto x = latchval->DEF(0);
      auto y = latchval->DEF(1);
      // Adding to self is taken into account before.
      if (y == phi)
        std::swap(x, y);
      if (x != phi)
        continue;

      if (!x->has<IncreaseAttr>() && y->has<IncreaseAttr>()) {
        auto incr = y->get<IncreaseAttr>();
        // At this point, all IncreaseAttrs we've plugged in
        // are all for induction variables.
        assert(incr->amt.size() == 1);
        std::vector val{ 0, incr->amt[0] };
        phi->add<IncreaseAttr>(val);
      }
    }
  }

  for (auto bb : info->getBlocks()) {
    for (auto op : bb->getOps()) {
      if (isa<StoreOp>(op))
        stores.push_back(op->DEF(1));
      if (isa<CallOp>(op) && op->has<ImpureAttr>())
        impure = true;
    }
  }

  // Don't rewrite operands of phi.
  // We don't need to introduce an extra phi for that - we've already got one.
  nochange.clear();
  for (auto bb : info->getBlocks()) {
    auto phis = bb->getPhis();
    for (auto phi : phis) {
      for (auto operand : phi->getOperands())
        nochange.insert(operand.defining);
    }
  }

  // Update IncreaseAttr according to op.
  // Do it in dominance-order, so that every def comes before use.
  rewrite(header, info);

  // Transform `addi` to `addl` and factor out the modulus.
  // This won't overflow because i32*i32 <= i64.
  auto exit = info->getExit();
  auto insert = nonphi(exit);
  
  std::unordered_map<Op*, Op*> exitlatch;
  // std::cerr << info << "\n";
  for (auto phi : exit->getPhis()) {
    // std::cerr << "phi: " << phi;
    exitlatch[Op::getPhiFrom(phi, latch)] = phi;
  }

  // Factor out the modulus.
  for (auto phi : mods) {
    auto mod = Op::getPhiFrom(phi, latch);
    auto latchphi = exitlatch.count(mod) ? exitlatch[mod] : exitlatch[phi];
    if (!latchphi) {
      std::cerr << "warning: bad?\n";
      continue;
    }

    auto addi = mod->DEF(0), v = mod->DEF(1);
    auto addl = builder.replace<AddLOp>(addi, addi->getOperands(), addi->getAttrs());
    mod->replaceAllUsesWith(addl);

    // Create a mod at the beginning of exit.
    builder.setBeforeOp(insert);
    auto modl = builder.create<ModLOp>(mod->getAttrs());
    latchphi->replaceAllUsesWith(modl);

    // We must push operands later, otherwise the operand itself will also be replaced.
    modl->pushOperand(latchphi);

    // Create a new IntOp to avoid dominance issues.
    builder.setBeforeOp(modl);
    auto vi = builder.create<IntOp>({ new IntAttr(V(v)) });
    modl->pushOperand(vi);
    
    mod->erase();
  }

  replaceAfter(info);

  // Remove IncreaseAttr for other loops to analyze.
  for (auto bb : info->getBlocks()) {
    for (auto op : bb->getOps())
      op->remove<IncreaseAttr>();
  }
}

void SCEV::discardIv(LoopInfo *info) {
  for (auto loop : info->subloops)
    discardIv(loop);

  if (info->latches.size() > 1)
    return;

  auto header = info->header;
  auto latch = info->getLatch();
  if (!isa<BranchOp>(latch->getLastOp()))
    return;
  if (isa<BranchOp>(header->getLastOp()))
    return;

  auto phis = header->getPhis();
  auto preheader = info->preheader;
  if (!preheader)
    return;

  if (info->exits.size() != 1)
    return;

  if (!info->getInduction())
    return;

  auto iv = info->getInduction();
  // Only referred to once at `addi`.
  if (iv->getUses().size() >= 2)
    return;

  auto stop = info->getStop();
  if (!stop || !stop->getParent()->dominates(preheader))
    return;

  Op *candidate = nullptr, *step, *start;
  // Try to identify a phi that also increases and is not the induction variable.
  for (auto phi : phis) {
    if (phi == iv)
      continue;

    auto latchval = Op::getPhiFrom(phi, latch);
    // Try to match (addl (x 'a)).
    if (constIncrL.match(latchval, { { "x", phi } })) {
      auto v = constIncrL.extract("'a");
      start = Op::getPhiFrom(phi, preheader);
      candidate = phi;
      step = v;
    }
  }

  if (!candidate)
    return;

  auto after = Op::getPhiFrom(candidate, latch);
  if (!after->getParent()->dominates(latch))
    return;

  auto oldstep = info->getStepOp();
  // The candidate's step is not a multiple of the old step;
  // we can't easily calculate the ending point.
  if (!oldstep || !isa<IntOp>(oldstep) || V(step) % V(oldstep))
    return;

  // We've identified a candidate. Now make a ending condition.
  Builder builder;
  builder.setBeforeOp(preheader->getLastOp());
  auto vi = builder.create<IntOp>({ new IntAttr(V(step) / V(oldstep)) });
  auto diff = builder.create<SubIOp>({ Value(stop), info->getStart() });
  auto mul = builder.create<MulIOp>({ diff, vi });
  auto end = builder.create<AddLOp>({ start, mul });

  // Replace the operand of the `br` to test (phi < end) instead.
  auto term = latch->getLastOp();
  builder.setBeforeOp(term);
  auto cond = builder.create<LtOp>({ after, end });
  term->setOperand(0, cond);
}

#define CREATE_ADD(...) \
  if (addl) \
    replace = builder.create<AddLOp>({ __VA_ARGS__ }); \
  else \
    replace = builder.create<AddIOp>({ __VA_ARGS__ })

void SCEV::replaceAfter(LoopInfo *info) {
  if (!info->getInduction())
    return;
  if (!isa<IntOp>(info->getStepOp()))
    return;

  auto header = info->header;
  auto preheader = info->preheader;
  auto exit = info->getExit();
  auto latch = info->getLatch();
  auto phis = header->getPhis();

  std::unordered_map<Op*, Op*> exitlatch;
  auto exitphis = exit->getPhis();
  for (auto phi : exitphis)
    exitlatch[Op::getPhiFrom(phi, latch)] = phi;

  auto start = info->getStart();
  auto stop = info->getStop();
  int step = info->getStep();
  if (!stop)
    return;

  Builder builder;

  // Add a block in between to ensure safety.
  // This destroys LCSSA.
  auto region = header->getParent();
  auto interm = region->insertAfter(latch);
  builder.setToBlockEnd(interm);
  builder.create<GotoOp>({ new TargetAttr(exit) });
  
  // Now the phi's from `latch` at exit should go from `interm`.
  for (auto phi : exitphis) {
    for (auto attr : phi->getAttrs()) {
      if (FROM(attr) == latch)
        FROM(attr) = interm;
    }
  }

  // The latch should go to interm.
  auto term = latch->getLastOp();
  if (TARGET(term) == exit)
    TARGET(term) = interm;
  if (ELSE(term) == exit)
    ELSE(term) = interm;

  // Now let's add things at the beginning of `interm`.
  builder.setToBlockStart(interm);

  for (auto phi : phis) {
    auto latchval = Op::getPhiFrom(phi, latch);
    bool addl = isa<AddLOp>(latchval);

    auto vstart = Op::getPhiFrom(phi, preheader);
    auto latchphi = exitlatch[latchval];
    if (!latchphi)
      continue;

    if (!phi->has<IncreaseAttr>()) {
      // Match repeated division as well.
      if (constDiv.match(latchval, { { "x", phi } })) {
        // To factor out the division, we need that the denominator is a power of 2.
        auto vi = V(constDiv.extract("'a"));
        if (__builtin_popcount(vi) != 1)
          continue;

        // Check the div is never used elsewhere in the loop.
        int usecnt = 0;
        for (auto use : phi->getUses()) {
          if (info->contains(use->getParent()))
            usecnt++;
        }
        if (usecnt != 1)
          continue;

        // Rewrite it as n / (1 << (ctz(vi) * ceil((stop - start) / step))).
        Value diff = builder.create<SubIOp>({ stop->getResult(), start });
        if (step != 1) {
          auto vi = builder.create<IntOp>({ new IntAttr(step - 1) });
          auto vj = builder.create<IntOp>({ new IntAttr(step) });
          auto add = builder.create<AddIOp>({ diff, vi });
          diff = builder.create<DivIOp>({ add, vj });
        }
        Value ctz = builder.create<IntOp>({ new IntAttr(__builtin_ctz(vi)) });
        Value mul = builder.create<MulIOp>({ ctz, diff });
        Value one = builder.create<IntOp>({ new IntAttr(1) });
        Value lsh = builder.create<LShiftLOp>({ one, mul });
        Value replace = builder.create<DivLOp>({ vstart, lsh });
        latchphi->replaceOperand(latchval, replace);
        continue;
      }

      // Match addition of loop-invariants.
      if (invarAdd.match(latchval, { { "x", phi } })) {
        auto incr = invarAdd.extract("y");
        // Make sure the value is loop-invariant.
        if (info->contains(incr->getParent()) || isa<LoadOp>(incr))
          continue;

        // Similar to the case where incr->amt.size() == 1 (see below).
        Value diff = builder.create<SubIOp>({ stop->getResult(), start });
        if (step != 1) {
          auto vi = builder.create<IntOp>({ new IntAttr(step - 1) });
          auto vj = builder.create<IntOp>({ new IntAttr(step) });
          auto add = builder.create<AddIOp>({ diff, vi });
          diff = builder.create<DivIOp>({ add, vj });
        }
        diff = builder.create<MulLOp>({ diff, incr });
        Op *replace;
        CREATE_ADD(vstart, diff);

        latchphi->replaceOperand(latchval, replace);
        continue;
      }

      continue;
    }

    // Match sizes.
    auto incr = phi->get<IncreaseAttr>();

    if (incr->amt.size() == 1) {
      // Final value:
      //   vstart + ceil((stop - start) / step) * incr->amt
      // = vstart + (stop - start + step - 1) / step * incr->amt
      Value diff = builder.create<SubIOp>({ stop->getResult(), start });
      if (step != 1) {
        auto vi = builder.create<IntOp>({ new IntAttr(step - 1) });
        auto vj = builder.create<IntOp>({ new IntAttr(step) });
        auto add = builder.create<AddIOp>({ diff, vi });
        diff = builder.create<DivIOp>({ add, vj });
      }
      if (incr->amt[0] != 1) {
        auto vstep = builder.create<IntOp>({ new IntAttr(incr->amt[0]) });
        diff = builder.create<MulIOp>({ diff, vstep });
      }

      Op *replace;
      CREATE_ADD(vstart, diff);

      latchphi->replaceOperand(latchval, replace);
    }

    if (incr->amt.size() == 2) {
      // diff = ceil((stop - start) / step)
      // final value =
      //   vstart + diff * (diff - 1) / 2 * incr->amt[1] + diff * incr->amt[0]
      
      Value diff = builder.create<SubIOp>({ stop->getResult(), start });
      if (step != 1) {
        auto vi = builder.create<IntOp>({ new IntAttr(step - 1) });
        auto vj = builder.create<IntOp>({ new IntAttr(step) });
        auto add = builder.create<AddIOp>({ diff, vi });
        diff = builder.create<DivIOp>({ add, vj });
      }

      Value replace = vstart;
      // The `diff * incr->amt[0]` part
      Value amt0 = diff;
      if (incr->amt[0] != 1) {
        auto vstep = builder.create<IntOp>({ new IntAttr(incr->amt[0]) });
        amt0 = builder.create<MulIOp>({ amt0, vstep });
      }
      CREATE_ADD(replace, amt0);

      // The `diff * (diff + 1) / 2` part
      auto one = builder.create<IntOp>({ new IntAttr(1) });
      auto add = builder.create<SubIOp>({ diff, one });
      auto mul = builder.create<MulLOp>({ diff, add });
      // It doesn't matter even if `diff < -1`, because no rounding is needed.
      Value amt1 = builder.create<RShiftOp>({ mul, one });
      if (incr->amt[1] != 1) {
        auto vstep = builder.create<IntOp>({ new IntAttr(incr->amt[1]) });
        amt1 = builder.create<MulIOp>({ amt1, vstep });
      }
      CREATE_ADD(replace, amt1);

      latchphi->replaceOperand(latchval, replace);
    }
  }
}

void SCEV::run() {
  LoopAnalysis analysis(module);
  analysis.run();
  auto forests = analysis.getResult();

  auto funcs = collectFuncs();
  module->dump();
  
  for (auto func : funcs) {
    const auto &forest = forests[func];
    auto region = func->getRegion();
    domtree = getDomTree(region);

    for (auto loop : forest.getLoops()) {
      if (!loop->getParent())
        runImpl(loop);
    }
  }

  AggressiveDCE(module).run();
  for (auto func : funcs) {
    const auto &forest = forests[func];

    for (auto loop : forest.getLoops()) {
      if (!loop->subloops.size())
        discardIv(loop);
    }
  }
}
