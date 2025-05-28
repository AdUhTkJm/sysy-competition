#include "LoopPasses.h"
#include "CleanupPasses.h"
#include "../utils/Matcher.h"

using namespace sys;

std::map<std::string, int> SCEV::stats() {
  return {
    { "expanded", expanded }
  };
}

static Rule constIncr("(add x 'a)");
static Rule constIncrL("(addl x 'a)");
static Rule modIncr("(mod (add x y) 'a)");

void SCEV::rewrite(BasicBlock *bb, LoopInfo *info) {
  auto preheader = info->getPreheader();
  auto header = info->getHeader();
  auto latch = info->getLatch();

  for (auto op : bb->getOps()) {
    if (op->has<IncreaseAttr>())
      continue;

    if (isa<AddIOp>(op) || isa<AddLOp>(op)) {
      auto x = op->DEF(0);
      auto y = op->DEF(1);
      if (!x->has<IncreaseAttr>()) {
        if (y->has<IncreaseAttr>())
          std::swap(x, y);
        else
          continue;
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
      if (!x->has<IncreaseAttr>()) {
        if (y->has<IncreaseAttr>())
          std::swap(x, y);
        else
          continue;
      }

      // Case 1. x * 'a
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
  Builder builder;
  builder.setBeforeOp(term);

  std::vector<Op*> candidates;
  for (auto op : bb->getOps()) {
    if (isa<PhiOp>(op) || !op->has<IncreaseAttr>())
      continue;

    if (nochange.count(op))
      continue;

    candidates.push_back(op);
  }

  // Const-unroll might introduce a lot of new increasing things,
  // if the unrolled loop have a surrounding loop.
  // We don't want to assign a phi to each of them.
  // We use a threshold to guard against this.
  if (candidates.size() >= 6) {
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

    auto amt = INCR(op)->amt;
    if (amt.size() > 1) {
      std::cerr << "cannot deal with amt.size() > 1\n";
      assert(false);
    }

    builder.setBeforeOp(op);
    auto vi = builder.create<IntOp>({ new IntAttr(amt[0]) });

    Op *add = builder.create<AddLOp>({ phi, vi });

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
  for (auto loop : info->getSubloops())
    runImpl(loop);

  if (info->getLatches().size() > 1)
    return;

  auto header = info->getHeader();
  auto latch = info->getLatch();
  // Check rotated loops.
  if (!isa<BranchOp>(latch->getLastOp()))
    return;
  if (isa<BranchOp>(header->getLastOp()))
    return;

  auto phis = header->getPhis();
  auto preheader = info->getPreheader();
  if (!preheader)
    return;

  if (info->getExits().size() != 1)
    return;

  // Inspect phis to find the amount by which something increases.
  start.clear();
  std::unordered_set<Op*> mods;
  for (auto phi : phis) {
    auto latchval = Op::getPhiFrom(phi, latch);
    // Try to match (add (x 'a)).
    if (constIncr.match(latchval, { { "x", phi } })) {
      auto v = constIncr.extract("'a");
      phi->add<IncreaseAttr>(V(v));

      // Also find out the start value.
      start[phi] = Op::getPhiFrom(phi, preheader);
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
  Builder builder;
  auto exit = info->getExit();
  auto insert = nonphi(exit);
  
  std::unordered_map<Op*, Op*> exitlatch;
  for (auto phi : exit->getPhis()) 
    exitlatch[Op::getPhiFrom(phi, latch)] = phi;

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
    modl->pushOperand(v);
    mod->erase();
  }

  // Remove IncreaseAttr for other loops to analyze.
  for (auto bb : info->getBlocks()) {
    for (auto op : bb->getOps())
      op->remove<IncreaseAttr>();
  }
}

void SCEV::discardIv(LoopInfo *info) {
  for (auto loop : info->getSubloops())
    discardIv(loop);

  if (info->getLatches().size() > 1)
    return;

  auto header = info->getHeader();
  auto latch = info->getLatch();
  if (!isa<BranchOp>(latch->getLastOp()))
    return;
  if (isa<BranchOp>(header->getLastOp()))
    return;

  auto phis = header->getPhis();
  auto preheader = info->getPreheader();
  if (!preheader)
    return;

  if (info->getExits().size() != 1)
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

  int oldstep = info->getStep();
  // The candidate's step is not a multiple of the old step;
  // we can't easily calculate the ending point.
  if (!oldstep || V(step) % oldstep)
    return;

  // We've identified a candidate. Now make a ending condition.
  Builder builder;
  builder.setBeforeOp(preheader->getLastOp());
  auto vi = builder.create<IntOp>({ new IntAttr(V(step) / oldstep) });
  auto diff = builder.create<SubIOp>({ Value(stop), info->getStart() });
  auto mul = builder.create<MulIOp>({ diff, vi });
  auto end = builder.create<AddLOp>({ start, mul });

  // Replace the operand of the `br` to test (phi < end) instead.
  auto term = latch->getLastOp();
  builder.setBeforeOp(term);
  auto cond = builder.create<LtOp>({ after, end });
  term->setOperand(0, cond);
}

void SCEV::run() {
  LoopAnalysis analysis(module);
  analysis.run();
  auto forests = analysis.getResult();

  auto funcs = collectFuncs();
  
  for (auto func : funcs) {
    const auto &forest = forests[func];
    auto region = func->getRegion();
    domtree = getDomTree(region);

    for (auto loop : forest.getLoops()) {
      if (!loop->getParent())
        runImpl(loop);
    }
  }

  // return;
  AggressiveDCE(module).run();
  for (auto func : funcs) {
    const auto &forest = forests[func];

    for (auto loop : forest.getLoops()) {
      if (!loop->getSubloops().size())
        discardIv(loop);
    }
  }
}
