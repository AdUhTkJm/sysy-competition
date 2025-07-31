#include "LoopPasses.h"

using namespace sys;

std::map<std::string, int> LICM::stats() {
  return {
    { "hoisted", hoisted }
  };
}

namespace {

// Pinned operations cannot move.
// Note that it's slightly different from GCM.cpp in that Load is not pinned.
#define PINNED(Ty) || isa<Ty>(op)
bool pinned(Op *op) {
  return (isa<CallOp>(op) && op->has<ImpureAttr>())
    PINNED(ReturnOp)
    PINNED(BranchOp)
    PINNED(GotoOp)
    PINNED(AllocaOp);
}


bool noAlias(Op *load, const std::vector<Op*> &stores) {
  auto addr = load->DEF();
  // Sometimes we don't have stores while unable to analyze loads.
  // It's safe to say no alias anyway.
  if (!stores.size())
    return true;

  while (isa<PhiOp>(addr)) {
    // Conservatively assume alias.
    if (addr->getOperandCount() >= 2)
      return false;

    addr = addr->DEF();
  }

  for (auto store : stores) {
    if (mayAlias(addr, store))
      return false;
  }
  return true;
}

// Traces `op` up back, and determines if everything between `info` and `outer` are invariant.
bool variant(Op *op, LoopInfo *info, LoopInfo *outer, int depth = 0) {
  // Somehow hit a loop. Why?
  if (depth >= 100)
    return true;

  // These will be marked as variant, but they don't affect anything.
  if (isa<BranchOp>(op) || isa<GotoOp>(op) || isa<ReturnOp>(op) || isa<AllocaOp>(op))
    return false;

  if (op->has<VariantAttr>())
    return true;

  if (isa<PhiOp>(op)) {
    auto parent = op->getParent();
    // For header phis, the only concern would be preheader.
    // Don't trap in an infinite loop.
    if (parent == info->header)
      return variant(Op::getPhiFrom(op, info->preheader), info, outer);
  }

  for (auto operand : op->getOperands()) {
    auto def = operand.defining;
    auto parent = def->getParent();
    if (parent->dominates(outer->header) && parent != outer->header)
      continue;
    // We can't hoist out things that are between `outer` and `info`.
    if (parent->dominates(info->header) && parent != info->header)
      return false;
    if (variant(def, info, outer, depth + 1))
      return true;
  }
  return false;
}

}

// This also hoists ops besides giving variant.
void LICM::hoistVariant(LoopInfo *info, BasicBlock *bb, bool hoistable) {
  std::vector<Op*> invariant;

  for (auto op : bb->getOps()) {
    if (isa<LoadOp>(op) || isa<BranchOp>(op))
      hoistable = false;

    if (op->has<VariantAttr>())
      continue;

    if (pinned(op) || isa<PhiOp>(op) || (isa<LoadOp>(op) && (!noAlias(op, stores) || impure))
        // When a store only writes a loop-invariant value to loop-invariant address,
        // and it doesn't follow any load, then it's safe to hoist it out.
        || (isa<StoreOp>(op) && (!hoistable || impure || op->DEF(0)->has<VariantAttr>() || op->DEF(1)->has<VariantAttr>())))
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
  auto term = info->preheader->getLastOp();
  for (auto op : invariant)
    op->moveBefore(term);

  for (auto child : domtree[bb]) {
    if (info->contains(child))
      hoistVariant(info, child, hoistable);
  }
}

void LICM::markVariant(LoopInfo *info, BasicBlock *bb, bool hoistable) {
  for (auto op : bb->getOps()) {
    if (isa<LoadOp>(op) || isa<BranchOp>(op))
      hoistable = false;

    if (op->has<VariantAttr>())
      continue;

    if (pinned(op) || (isa<LoadOp>(op) && (!noAlias(op, stores) || impure))
        || (isa<StoreOp>(op) && (!hoistable || impure || op->DEF(0)->has<VariantAttr>() || op->DEF(1)->has<VariantAttr>())))
      op->add<VariantAttr>();
    else for (auto operand : op->getOperands()) {
      auto def = operand.defining;
      if (def->has<VariantAttr>()) {
        op->add<VariantAttr>();
        break;
      }
    }
  }

  for (auto child : domtree[bb]) {
    if (info->contains(child))
      markVariant(info, child, hoistable);
  }
}

bool LICM::updateStores(LoopInfo *info) {
  // Check rotated loops.
  auto preheader = info->preheader;
  if (!preheader)
    return false;

  for (auto latch : info->latches) {
    auto term = latch->getLastOp();
    if (!isa<BranchOp>(term))
      return false;
  }

  // Record all stores in the loop.
  stores.clear();
  impure = false;
  for (auto bb : info->getBlocks()) {
    for (auto op : bb->getOps()) {
      if (isa<StoreOp>(op))
        stores.push_back(op->DEF(1));
      if (isa<CallOp>(op) && op->has<ImpureAttr>())
        impure = true;
    }
  }
  return true;
}

void LICM::runImpl(LoopInfo *info) {
  // Hoist internal loops first;
  // otherwise we risk hoisting inner-loop's variants out.
  for (auto subloop : info->subloops)
    runImpl(subloop);

  if (!updateStores(info))
    return;

  // Mark invariants inside the loop, and try hoisting it out.
  // We must traverse through domtree to preserve def-use chain.
  auto header = info->header;
  hoistVariant(info, header, /*hoistable=*/true);
}

bool LICM::hoistSubloop(LoopInfo *outer) {
  if (!updateStores(outer))
    return false;

  for (auto bb : outer->getBlocks()) {
    // All single-operand phis that refer to things outside this loop can be folded.
    // This won't break LCSSA.
    auto ops = bb->getOps();
    for (auto op : ops) {
      if (!isa<PhiOp>(op) || op->getOperandCount() != 1)
        continue;

      auto def = op->DEF();
      if (def->getParent()->dominates(outer->preheader)) {
        op->replaceAllUsesWith(def);
        op->erase();
      }
    }
  }

  auto phis = outer->header->getPhis();
  for (auto phi : phis)
    phi->add<VariantAttr>();

  markVariant(outer, outer->header, /*hoistable=*/true);

  for (auto loop : outer->subloops) {
    if (loop->exits.size() > 1 || loop->latches.size() > 1)
      continue;

    // Check for rotated.
    auto latch = loop->getLatch();
    if (!isa<BranchOp>(latch->getLastOp()))
      continue;

    bool good = true;
    for (auto bb : loop->getBlocks()) {
      for (auto op : bb->getOps()) {
        if (variant(op, loop, outer)) {
          good = false;
          break;
        }
      }
    }
    if (!good)
      continue;

    Builder builder;

    // Redirect preheader to the hoisted loop's header.
    auto preheader = outer->preheader;
    auto region = preheader->getParent();
    auto prterm = preheader->getLastOp();
    builder.replace<GotoOp>(prterm, { new TargetAttr(loop->header) });

    // The previous preheader should now get to exit.
    auto exit = loop->getExit();
    prterm = loop->preheader->getLastOp();
    builder.replace<GotoOp>(prterm, { new TargetAttr(exit) });
    
    // Hoist the loop out.
    // Move every block before outer's header.
    for (auto bb : loop->getBlocks())
      bb->moveBefore(outer->header);

    // Create outer loop's new preheader as loop exit.
    auto newexit = region->insert(outer->header);
    auto latchterm = latch->getLastOp();

    if (TARGET(latchterm) == exit)
      TARGET(latchterm) = newexit;
    if (ELSE(latchterm) == exit)
      ELSE(latchterm) = newexit;

    // The phi's at original exit should be moved to the new exit.
    auto phis = exit->getPhis();
    for (auto phi : phis)
      phi->moveToStart(newexit);

    // New exit should go to outer loop header.
    builder.setToBlockEnd(newexit);
    builder.create<GotoOp>({ new TargetAttr(outer->header) });

    // For both header and exit, rewire preheader to outer's old preheader.
    auto rewire = loop->header->getPhis();
    rewire.reserve(rewire.size() + phis.size());
    std::copy(phis.begin(), phis.end(), std::back_inserter(rewire));

    for (auto phi : rewire) {
      phi->remove<VariantAttr>();
      for (auto attr : phi->getAttrs()) {
        if (FROM(attr) == loop->preheader) {
          FROM(attr) = outer->preheader;
          break;
        }
      }
    }

    // For outer loop's header, it's preheader is now from newexit.
    rewire = outer->header->getPhis();
    for (auto phi : rewire) {
      phi->remove<VariantAttr>();
      for (auto attr : phi->getAttrs()) {
        if (FROM(attr) == outer->preheader) {
          FROM(attr) = newexit;
          break;
        }
      }
    }
    return true;
  }
  return false;
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

  for (auto func : funcs) {
    auto region = func->getRegion();
    domtree = getDomTree(region);

    auto forest = forests[func];
    bool changed;
    do {
      changed = false;
      for (auto info : forest.getLoops()) {
        // Only call for top-level loops.
        if (!info->getParent() && hoistSubloop(info)) {
          forest = loop.runImpl(region);
          domtree = getDomTree(region);
          changed = true;
          break;
        }
      }

      for (auto bb : region->getBlocks()) {
        for (auto op : bb->getOps())
          op->remove<VariantAttr>();
      }
    } while (changed);
  }
}
