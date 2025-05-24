#include "LoopPasses.h"
#include "../utils/Matcher.h"
#include <unordered_set>

using namespace sys;

std::map<std::string, int> SCEV::stats() {
  return {
    { "expanded", expanded }
  };
}

// Defined in LICM.cpp.
bool noAlias(Op *load, const std::vector<Op*> stores);

static Rule constIncr("(add x 'a)");

void SCEV::rewrite(BasicBlock *bb, LoopInfo *info) {
  auto preheader = info->getPreheader();
  auto header = info->getHeader();
  auto latch = info->getLatch();

  for (auto op : bb->getOps()) {
    if (op->has<VariantAttr>())
      continue;

    if (isa<LoadOp>(op) && (!noAlias(op, stores) || impure))
      op->add<VariantAttr>();
    else for (auto operand : op->getOperands()) {
      auto def = operand.defining;
      if (def->has<VariantAttr>()) {
        op->add<VariantAttr>();
        break;
      }
    }
  }

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
      if (!y->has<VariantAttr>() && !isa<PhiOp>(y)) {
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

  // Try to find all operands in `start`.
  // Then create a `clone` which would be the starting value.
  std::vector<Op*> produced;

  for (auto op : bb->getOps()) {
    if (isa<PhiOp>(op) || !op->has<IncreaseAttr>())
      continue;

    if (nochange.count(op))
      continue;

    bool good = true;
    for (auto operand : op->getOperands()) {
      if (!start.count(operand.defining)) {
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
  
  for (auto child : domtree[bb])
    rewrite(child, info);
}

void SCEV::runImpl(LoopInfo *info) {
  if (info->getLatches().size() > 1)
    return;

  auto header = info->getHeader();
  auto latch = info->getLatch();
  // Check rotated loops.
  if (!isa<BranchOp>(latch->getLastOp()))
    return;

  auto phis = header->getPhis();
  auto preheader = info->getPreheader();

  // Inspect phis to find the amount by which something increases.
  start.clear();
  for (auto phi : phis) {
    auto latchval = Op::getPhiFrom(phi, latch);
    // Try to match (add (x 'a)).
    if (constIncr.match(latchval, { { "x", phi } })) {
      auto v = constIncr.extract("'a");
      phi->add<IncreaseAttr>(V(v));

      // Also find out the start value.
      start[phi] = Op::getPhiFrom(phi, preheader);
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

    for (auto loop : forest.getLoops())
      runImpl(loop);
  }

  // Erase all IncreaseAttr from phis.
  // Phi rely on subscript to find FromAttr,
  // so no other attributes are allowed for them.
  for (auto func : funcs) {
    auto region = func->getRegion();
  
    for (auto bb : region->getBlocks()) {
      for (auto op : bb->getOps()) {
        op->remove<VariantAttr>();
      }
    }
  }
}
