#include "LoopPasses.h"
#include "Passes.h"

using namespace sys;

std::map<std::string, int> ConstLoopUnroll::stats() {
  return {
    { "unrolled", unrolled }
  };
}

BasicBlock *ConstLoopUnroll::copyLoop(LoopInfo *loop, BasicBlock *bb, int unroll) {
  std::map<Op*, Op*> cloneMap, revcloneMap, prevLatch;
  std::map<BasicBlock*, BasicBlock*> rewireMap;
  Builder builder;
  BasicBlock *latch = loop->getLatch();
  BasicBlock *lastLatch = loop->getLatch();
  BasicBlock *header = loop->getHeader();
  BasicBlock *exit = loop->getExit();
  BasicBlock *latchRewire = nullptr;
  auto region = lastLatch->getParent();

  // We're rewiring it to header, so we've got a copy.
  --unroll;

  for (int i = 0; i < unroll; i++) {
    cloneMap.clear();
    revcloneMap.clear();
    rewireMap.clear();
    
    // First shallow copy everything.
    std::vector<Op*> created;

    for (auto block : loop->getBlocks()) {
      bb = region->insertAfter(bb);
      builder.setToBlockStart(bb);
      for (auto op : block->getOps()) {
        auto copied = builder.copy(op);
        cloneMap[op] = copied;
        created.push_back(copied);
        if (isa<PhiOp>(op))
          revcloneMap[copied] = op;
      }
      rewireMap[block] = bb;
    }

    // Rewire operands.
    for (auto op : created) {
      auto operands = op->getOperands();
      op->removeAllOperands();
      for (auto operand : operands) {
        auto def = operand.defining;
        op->pushOperand(cloneMap.count(def) ? cloneMap[def] : def);
      }
    }

    // Rewire blocks.
    for (auto [k, v] : rewireMap) {
      auto term = v->getLastOp();
      if (auto attr = term->find<TargetAttr>(); attr && rewireMap.count(attr->bb))
        attr->bb = rewireMap[attr->bb];
      if (auto attr = term->find<ElseAttr>(); attr && rewireMap.count(attr->bb))
        attr->bb = rewireMap[attr->bb];
    }

    // The current (unappended) latch of the loop should connect to the new region instead.
    // We shouldn't change the original latch for side-loop; otherwise all future copies will break.
    auto term = lastLatch->getLastOp();
    auto rewired = rewireMap[header];
    if (lastLatch != latch)
      builder.replace<GotoOp>(term, { new TargetAttr(rewired) });
    else
      latchRewire = rewired;

    // The new latch now branches to either the rewire header or exit.
    // Redirect it to the real header.

    auto curLatch = rewireMap[latch];
    term = curLatch->getLastOp();
    if (TARGET(term) == rewired)
      TARGET(term) = header;
    if (ELSE(term) == rewired)
      ELSE(term) = header;

    // Update the current latch.
    lastLatch = curLatch;
    
    // Replace phis at header.
    // All phis come from either the preheader or the latch.
    // Now the "preheader" is the previous latch. 
    // The value wouldn't come from the latch because it's no longer a predecessor.
    auto phis = rewireMap[header]->getPhis();

    for (auto copiedphi : phis) {
      auto origphi = revcloneMap[copiedphi];
      // We should use the updated version of the variable.
      // This means the operand from latch in the original phi (phiMap[origphi]).
      auto latchvalue = phiMap[origphi];

      // For the block succeeding the original loop body, `prevLatch` is empty.
      // Just use the latch value.
      Op *value;
      if (!prevLatch.count(latchvalue))
        value = latchvalue;
      else 
        // Otherwise, use `prevLatch` (which is actually the cloneMap of the previous iteration)
        // to find the inherited value.
        value = prevLatch[latchvalue];

      copiedphi->replaceAllUsesWith(value);
      copiedphi->erase();
    }
    

    // All remaining phis should have their blocks renamed.
    // Note that `revcloneMap` contains all phis.
    std::set<Op*> erased(phis.begin(), phis.end());
    for (auto [k, _] : revcloneMap) {
      if (erased.count(k))
        continue;

      for (auto attr : k->getAttrs())
        FROM(attr) = rewireMap[FROM(attr)];
    }

    prevLatch = cloneMap;
  }

  // Rewire the old (first) latch now. It should go to `latchRewire`.
  auto term = latch->getLastOp();
  builder.replace<GotoOp>(term, { new TargetAttr(latchRewire) });

  // Also, the last latch can only get to exit. There's no chance of returning to header.
  auto final = lastLatch->getLastOp();
  builder.replace<GotoOp>(final, { new TargetAttr(exit) });

  // The exit can only receive operand from the latest latch.
  for (auto [k, v] : exitlatch) {
    Op *def = cloneMap.count(v) ? cloneMap[v] : v;
    
    int index = k->replaceOperand(v, def);
    k->setAttribute(index, new FromAttr(lastLatch));
  }

  // Phis at the header should also now point to the new latch.
  auto phis = header->getPhis();
  for (auto phi : phis) {
    const auto &ops = phi->getOperands();
    const auto &attrs = phi->getAttrs();

    // The side loop will only be executed once.
    // We remove the operand from latch.
    for (int i = 0; i < ops.size(); i++) {
      if (FROM(attrs[i]) == latch) {
        phi->removeOperand(i);
        phi->removeAttribute(i);
        break;
      }
    }
  }

  return lastLatch;
}

bool ConstLoopUnroll::runImpl(LoopInfo *loop) {
  if (!loop->getInduction())
    return false;

  if (loop->getExits().size() != 1)
    return false;

  auto header = loop->getHeader();
  auto preheader = loop->getPreheader();
  auto latch = loop->getLatch();
  // The loop is not rotated. Don't unroll it.
  if (!isa<BranchOp>(latch->getLastOp()))
    return false;

  auto exit = loop->getExit();
  // We don't want an internal `break` to interfere.
  // It should come from either preheader or latch.
  if (exit->preds.size() > 2)
    return false;

  int loopsize = 0;
  for (auto bb : loop->getBlocks())
    loopsize += bb->getOps().size();

  // Not every loop can be unrolled, even not all constant-bounded loops.
  // See 65_color.sy, where we are attempting to unroll a nested loop with a total of 18^5*7 = 13226976 iterations.
  if (loopsize > 300)
    return false;

  auto phis = header->getPhis();
  // Ensure that every phi at header is either from preheader or from the latch.
  // Also, finds the value of each phi from latch.
  phiMap.clear();
  for (auto phi : phis) {
    const auto &ops = phi->getOperands();
    const auto &attrs = phi->getAttrs();
    if (ops.size() != 2)
      return false;
    auto bb1 = cast<FromAttr>(attrs[0])->bb;
    auto bb2 = cast<FromAttr>(attrs[1])->bb;
    if (!(bb1 == latch && bb2 == preheader
       || bb2 == latch && bb1 == preheader))
      return false;

    if (bb1 == latch)
      phiMap[phi] = ops[0].defining;
    if (bb2 == latch)
      phiMap[phi] = ops[1].defining;
  }

  int unroll = -1;

  auto lower = loop->getStart();
  auto upper = loop->getStop();
  if (!upper)
    return false;

  // Check the real op.
  while (isa<PhiOp>(lower) && lower->getOperandCount() == 1)
    lower = lower->DEF();
  while (isa<PhiOp>(upper) && upper->getOperandCount() == 1)
    upper = upper->DEF();

  // Fully unroll constant-bounded loops if it's small enough.
  if (lower && upper && isa<IntOp>(lower) && isa<IntOp>(upper)) {
    int low = V(lower);
    int high = V(upper);
    if (high - low <= 1000 / loopsize)
      unroll = high - low;
  }
  // Not a constant loop.
  if (unroll == -1)
    return false;

  // If the constant-bounded loop has a single iteration, it'll be handled by regular fold.
  if (unroll == 1)
    return false;

  // Record the phi values at the beginning of `exit` that are taken from the latch.
  // Note that "taken from latch" doesn't necessarily mean it's in the loop.
  // It can be from something that passes through all the loop, for example.
  auto exitphis = exit->getPhis();
  exitlatch.clear();
  for (auto phi : exitphis) 
    exitlatch[phi] = Op::getPhiFrom(phi, latch);

  // We construct a side-loop with checks in it. The code is roughly the same.
  // Note that preheader won't change if it's used, because it's only used when `true`.
  copyLoop(loop, loop->getLatch(), unroll);

  unrolled++;
  return true;
}

static void postorder(LoopInfo *loop, std::vector<LoopInfo*> &order) {
  for (auto subloop : loop->getSubloops())
    order.push_back(subloop);
  order.push_back(loop);
}

void ConstLoopUnroll::run() {
  LoopAnalysis analysis(module);

  auto funcs = collectFuncs();
  for (auto func : funcs) {
    auto region = func->getRegion();
    auto forest = analysis.runImpl(region);

    bool changed;
    do {
      changed = false;
      // Don't unroll too much.
      if (region->getBlocks().size() > 1000)
        break;

      // We want to unroll small loops first.
      std::vector<LoopInfo*> order;
      for (auto loop : forest.getLoops()) {
        if (!loop->getParent())
          postorder(loop, order);
      }

      for (auto loop : order) {
        // We only want to unroll innermost loops.
        // Also, we can't unroll nested loops correctly.
        if (loop->getSubloops().size() > 0)
          continue;

        if (runImpl(loop)) {
          // We need to run a fold to completely flatten the loop.
          // (We've only copied the body without dealing with branches and phis.)
          forest = analysis.runImpl(region);
          changed = true;
          break;
        }
      }
    } while (changed);
  }
}
