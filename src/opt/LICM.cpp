#include "LoopPasses.h"

using namespace sys;

std::map<std::string, int> LICM::stats() {
  return {
    { "hoisted", hoisted }
  };
}

// Pinned operations cannot move.
// Note that it's slightly different from GCM.cpp in that Load is not pinned.
#define PINNED(Ty) || isa<Ty>(op)
static bool pinned(Op *op) {
  return (isa<CallOp>(op) && op->has<ImpureAttr>())
    PINNED(ReturnOp)
    PINNED(BranchOp)
    PINNED(GotoOp)
    PINNED(PhiOp)
    PINNED(AllocaOp);
}

static bool noAlias(Op *load, const std::vector<Op*> stores) {
  auto addr = load->DEF();
  auto alias = ALIAS(addr);
  for (auto store : stores) {
    if (ALIAS(store)->mayAlias(alias))
      return false;
  }
  return true;
}

// This also hoists ops besides giving variant.
void LICM::markVariant(LoopInfo *info, BasicBlock *bb, bool hoistable) {
  std::vector<Op*> invariant;

  for (auto op : bb->getOps()) {
    if (isa<LoadOp>(op) || isa<BranchOp>(op))
      hoistable = false;

    if (op->has<VariantAttr>())
      continue;

    if (pinned(op) || (isa<LoadOp>(op) && !noAlias(op, stores))
        // When a store only writes a loop-invariant value to loop-invariant address,
        // and it doesn't follow any load, then it's safe to hoist it out.
        || (isa<StoreOp>(op) && (!hoistable || op->DEF(0)->has<VariantAttr>() || op->DEF(1)->has<VariantAttr>())))
      op->add<VariantAttr>();
    else for (auto operand : op->getOperands()) {
      auto def = operand.defining;
      if (def->has<VariantAttr>()) {
        op->add<VariantAttr>();
        break;
      }
    }

    if (!op->has<VariantAttr>())
      invariant.push_back(op);
  }

  // Now hoist everything to preheader.
  hoisted += invariant.size();
  auto term = info->getPreheader()->getLastOp();
  for (auto op : invariant)
    op->moveBefore(term);

  for (auto child : domtree[bb]) {
    if (info->contains(child))
      markVariant(info, child, hoistable);
  }
}

void LICM::runImpl(LoopInfo *info) {
  // Hoist internal loops first;
  // otherwise we risk hoisting inner-loop's variants out.
  for (auto subloop : info->getSubloops())
    runImpl(subloop);

  // Check rotated loops.
  auto preheader = info->getPreheader();
  if (!preheader)
    return;

  for (auto latch : info->getLatches()) {
    auto term = latch->getLastOp();
    if (!isa<BranchOp>(term))
      return;
  }

  // Record all stores in the loop.
  stores.clear();
  for (auto bb : info->getBlocks()) {
    for (auto op : bb->getOps()) {
      if (isa<StoreOp>(op))
        stores.push_back(op->DEF(1));
    }
  }

  // Mark invariants inside the loop, and try hoisting it out.
  // We must traverse through domtree to preserve def-use chain.
  auto header = info->getHeader();
  markVariant(info, header, true);
}

void LICM::run() {
  LoopAnalysis loop(module);
  loop.run();
  auto forests = loop.getResult();

  auto funcs = collectFuncs();
  
  for (auto func : funcs) {
    auto region = func->getRegion();
    domtree = getDomTree(region);

    const auto &forest = forests[func];
    for (auto info : forest.getLoops()) {
      // Only call for top-level loops.
      if (!info->getParent())
        runImpl(info);
    }

    // Remove VariantAttr's attached.
    // That's necessary because phi's cannot have attrs other than FromAttr.
    for (auto bb : region->getBlocks()) {
      for (auto op : bb->getOps())
        op->remove<VariantAttr>();
    }
  }
}
