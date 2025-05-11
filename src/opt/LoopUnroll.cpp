#include "LoopPasses.h"

using namespace sys;

std::map<std::string, int> LoopUnroll::stats() {
  return {
    { "unrolled", unrolled }
  };
}

// TODO: Still buggy. Try unroll = 3.
bool LoopUnroll::runImpl(LoopInfo *loop) {
  if (!loop->getInduction())
    return false;

  if (loop->getExits().size() > 1)
    return false;

  auto header = loop->getHeader();
  auto preheader = loop->getPreheader();
  auto latch = loop->getLatch();
  // The loop is not rotated. Don't unroll it.
  if (!isa<BranchOp>(latch->getLastOp()))
    return false;

  auto phis = header->getPhis();
  // Ensure that every phi at header is either from preheader or from the latch.
  // Also, finds the value of each phi from latch.
  std::map<Op*, Op*> phiMap;
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

  int loopsize = 0;
  for (auto bb : loop->getBlocks())
    loopsize += bb->getOps().size();
  int unroll = 2; //loopsize > 500 ? 2 : loopsize > 200 ? 4 : 8;

  // Whether the loop is flattened completely, into serial execution.
  bool complete = false;

  auto lower = loop->getStart();
  auto upper = loop->getStop();
  // Fully unroll constant-bounded loops if it's small enough.
  if (lower && upper && isa<IntOp>(lower) && isa<IntOp>(upper)) {
    int low = V(lower);
    int high = V(upper);
    if (high - low <= 32) {
      unroll = high - low;
      complete = true;
    }
  }
  
  // Replicate every block.
  auto bb = loop->getLatch();
  auto region = bb->getParent();

  // Record the phi values at the beginning of `exit` that are taken from the latch.
  auto exit = *loop->getExits().begin();
  std::map<Op*, Op*> exitlatch;
  auto exitphis = exit->getPhis();
  for (auto phi : exitphis) {
    const auto &ops = phi->getOperands();
    const auto &attrs = phi->getAttrs();
    for (int i = 0; i < ops.size(); i++) {
      if (FROM(attrs[i]) == latch)
        exitlatch[phi] = ops[i].defining;
    }
  }

  std::map<Op*, Op*> cloneMap, revcloneMap, prevLatch;
  std::map<BasicBlock*, BasicBlock*> rewireMap;
  Builder builder;
  BasicBlock *lastLatch = latch;
  BasicBlock *latchRewire = nullptr;

  // We have already got one copy. So only replicate `unroll - 1` times.
  while (--unroll) {
    cloneMap.clear();
    revcloneMap.clear();
    rewireMap.clear();

    // First shallow copy everything.
    std::vector<Op*> created;
    created.reserve(loopsize);

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
    // We shouldn't change the original latch; otherwise all future copies will break.
    auto term = lastLatch->getLastOp();
    auto rewired = rewireMap[header];
    if (lastLatch != latch) {
      if (TARGET(term) == header)
        TARGET(term) = rewired;
      if (ELSE(term) == header)
        ELSE(term) = rewired;
    } else latchRewire = rewired;

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
      if (!prevLatch.count(latchvalue)) {
        copiedphi->replaceAllUsesWith(latchvalue);
        copiedphi->erase();
        continue;
      }

      // Otherwise, use `prevLatch` (which is actually the cloneMap of the previous iteration)
      // to find the inherited value.
      copiedphi->replaceAllUsesWith(prevLatch[latchvalue]);
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

    // Fix exit phis.
    // A new predecessor is added to exit, so we need to add another operand.
    for (auto [k, v] : exitlatch) {
      auto operand = cloneMap[v];
      k->pushOperand(operand);
      k->add<FromAttr>(curLatch);
    }

    prevLatch = cloneMap;
  }

  // Rewire the old latch now. It should go to `latchRewire`.
  auto term = latch->getLastOp();
  if (TARGET(term) == header)
    TARGET(term) = latchRewire;
  if (ELSE(term) == header)
    ELSE(term) = latchRewire;

  // Phis at the header should also now point to the new latch.
  phis = header->getPhis();
  for (auto phi : phis) {
    const auto &ops = phi->getOperands();
    const auto &attrs = phi->getAttrs();

    if (complete) {
      // If this is a complete flattening, then the phi would only come from preheader.
      for (int i = 0; i < ops.size(); i++) {
        if (FROM(attrs[i]) != preheader)
          continue;

        phi->replaceAllUsesWith(ops[i].defining);
        phi->erase();
        break;
      }
      continue;
    }

    for (int i = 0; i < ops.size(); i++) {
      if (FROM(attrs[i]) != latch)
        continue;

      FROM(attrs[i]) = lastLatch;
      // phiMap[phi] is the latch-value in the original phi.
      // cloneMap[phiMap[phi]] is the latch-value in the last-copied block.
      phi->setOperand(i, cloneMap[phiMap[phi]]);
      break;
    }
  }

  unrolled++;
  return true;
}

void LoopUnroll::run() {
  LoopAnalysis analysis(module);

  auto funcs = collectFuncs();
  for (auto func : funcs) {
    auto region = func->getRegion();
    auto forest = analysis.runImpl(region);

    bool changed;
    do {
      changed = false;
      for (auto loop : forest.getLoops()) {
        if (runImpl(loop)) {
          forest = analysis.runImpl(region);
          changed = true;
          break;
        }
      }
    } while (changed);
  }
}
