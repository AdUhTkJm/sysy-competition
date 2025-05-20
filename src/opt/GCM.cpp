#include "Passes.h"
#include "LoopPasses.h"

using namespace sys;

// Pinned operations cannot move.
#define PINNED(Ty) || isa<Ty>(op)
bool pinned(Op *op) {
  return (isa<CallOp>(op) && op->has<ImpureAttr>())
    PINNED(LoadOp)
    PINNED(StoreOp)
    PINNED(ReturnOp)
    PINNED(BranchOp)
    PINNED(GotoOp)
    PINNED(PhiOp)
    PINNED(AllocaOp);
}

// Schedule `op` to the first block that is dominated by its inputs.
void GCM::scheduleEarly(BasicBlock *entry, Op *op) {
  if (visited.count(op) || pinned(op))
    return;
  visited.insert(op);

  // The unit is the entry block. First move it there.
  auto term = entry->getLastOp();
  op->moveBefore(term);

  for (auto operand : op->getOperands()) {
    auto def = operand.defining;
    scheduleEarly(entry, def);

    auto defbb = def->getParent();
    auto bb = op->getParent();
    if (depth[defbb] > depth[bb]) {
      auto term = defbb->getLastOp();
      op->moveBefore(term);
    }
  }
}

// Schedule `op` to the latest block that dominates its uses, but as out-of-loop as possible.
void GCM::scheduleLate(Op *op) {
  if (visited.count(op) || pinned(op))
    return;
  visited.insert(op);

  BasicBlock *lca = nullptr;
  for (auto use : op->getUses()) {
    scheduleLate(use);

    // We consider Phi to be from the previous block, just as we did in CanonicalizeLoop.
    BasicBlock *usebb = use->getParent();
    if (isa<PhiOp>(use)) {
      for (size_t i = 0; i < use->getOperands().size(); i++) {
        if (use->getOperand(i).defining == op) {
          usebb = cast<FromAttr>(use->getAttrs()[i])->bb;
          break;
        }
      }
    }

    lca = this->lca(usebb, lca);
  }

  // Now the range available is the current position (`parent`) to `lca`.
  // Note that `lca` is actually lower than `parent` despite the name.
  // Also note `lca` can be nullptr when there's no uses.
  auto parent = op->getParent();
  if (lca) {
    // Try to enumerate the path between `lca` and `parent`,
    // and find the outermost loop.
    BasicBlock *result = lca;
    while (lca != parent) {
      lca = lca->getIdom();
      if (loopDepth[lca] < loopDepth[result])
        result = lca;
    }

    auto term = result->getLastOp();
    op->moveBefore(term);
  }

  // If `op` is used in its current block, make sure it goes before its uses.
  parent = op->getParent();
  for (auto x : parent->getOps()) {
    if (x != op && !isa<PhiOp>(x) && op->getUses().count(x)) {
      // Move `op` before its first use in the block.
      op->moveBefore(x);
      break;
    }
  }
}

void GCM::updateDepth(BasicBlock *bb, int dep) {
  depth[bb] = dep;
  for (auto child : tree[bb])
    updateDepth(child, dep + 1);
}

void GCM::updateLoopDepth(LoopInfo *info, int dep) {
  for (auto bb : info->getBlocks())
    loopDepth[bb] = dep;

  for (auto subloop : info->getSubloops())
    updateLoopDepth(subloop, dep + 1);
}

static void postorder(BasicBlock *current, DomTree &tree, std::vector<BasicBlock*> &order) {
  for (auto child : tree[current])
    postorder(child, tree, order);
  order.push_back(current);
}

// A naive approach.
// LCA can be reduced to range minimum query, but for a small graph, who cares?
BasicBlock *GCM::lca(BasicBlock *a, BasicBlock *b) {
  if (!a)
    return b;
  if (!b)
    return a;
  while (depth[a] > depth[b])
    a = a->getIdom();
  while (depth[b] > depth[a])
    b = b->getIdom();
  while (a != b) {
    a = a->getIdom();
    b = b->getIdom();
  }
  return a;
}

void GCM::runImpl(Region *region, const LoopForest &forest) {
  visited.clear();
  depth.clear();

  tree = getDomTree(region);
  auto entry = region->getFirstBlock();
  updateDepth(entry, 0);

  // Note that blocks outside loop should have a depth 0.
  for (auto loop : forest.getLoops())
    updateLoopDepth(loop, 1);

  std::vector<BasicBlock*> rpo;
  postorder(entry, tree, rpo);
  std::reverse(rpo.begin(), rpo.end());

  std::vector<Op*> toSched;
  for (auto bb : rpo) {
    for (auto op : bb->getOps()) {
      if (pinned(op))
        continue;

      toSched.push_back(op);
    }
  }

  for (auto op : toSched)
    scheduleEarly(entry, op);

  std::reverse(toSched.begin(), toSched.end());
  visited.clear();

  for (auto op : toSched)
    scheduleLate(op);
}

// See https://courses.cs.washington.edu/courses/cse501/06wi/reading/click-pldi95.pdf
// Global Code Motion, by Cliff Click
void GCM::run() {
  LoopAnalysis loop(module);
  loop.run();
  auto forests = loop.getResult();
  
  auto funcs = collectFuncs();
  
  for (auto func : funcs)
    runImpl(func->getRegion(), forests[func]);
}
