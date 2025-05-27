#include "LoopPasses.h"
#include "../utils/Matcher.h"

using namespace sys;

std::map<std::string, int> SCEV::stats() {
  return {
    { "expanded", expanded }
  };
}

static Rule constIncr("(add x 'a)");
static Rule modIncr("(mod (add x y) 'a)");

void SCEV::rewrite(BasicBlock *bb, LoopInfo *info) {
  auto preheader = info->getPreheader();
  auto header = info->getHeader();
  auto latch = info->getLatch();

  for (auto op : bb->getOps()) {
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
        assert(nochange.count(def));
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
  for (auto op : produced) {
    builder.setToBlockStart(header);
    auto phi = builder.create<PhiOp>({ start[op] }, { new FromAttr(preheader) });

    auto amt = INCR(op)->amt;
    if (amt.size() > 1) {
      std::cerr << "cannot deal with amt.size() > 1\n";
      assert(false);
    }

    builder.setBeforeOp(op);
    auto vi = builder.create<IntOp>({ new IntAttr(amt[0]) });

    Op *add;
    if (isa<AddLOp>(op))
      add = builder.create<AddLOp>({ phi, vi });
    else
      add = builder.create<AddIOp>({ phi, vi });

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
  for (auto subloop : info->getSubloops())
    runImpl(subloop);

  if (info->getLatches().size() > 1)
    return;

  auto header = info->getHeader();
  auto latch = info->getLatch();
  // Check rotated loops.
  if (!isa<BranchOp>(latch->getLastOp()))
    return;
  

  auto phis = header->getPhis();
  auto preheader = info->getPreheader();
  if (!preheader)
    return;

  if (info->getExits().size() != 1)
    return;

  // Latch and header must point to the same block.
  // (See h_functional/09_BFS for a situation where this isn't true.)
  if (!ordinary(info))
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
}
