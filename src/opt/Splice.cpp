#include "LoopPasses.h"
#include "Analysis.h"

using namespace sys;

// Defined in Specialize.cpp.
void removeRange(Region *region);

// Identify this pattern:
//   %1 = phi %2 %3
// ...
//   %2 = ... <range = a>
//   %3 = ... <range = b>
//
// where `a` and `b` are constants.
// This strongly suggests that the first iteration is different
// and is worth splicing.
void Splice::runImpl(LoopInfo *loop) {
  if (loop->latches.size() > 1 || !loop->preheader)
    return;

  auto latch = loop->getLatch();
  // The loop should be rotated.
  if (!isa<BranchOp>(latch->getLastOp()))
    return;

  auto induction = loop->getInduction();
  if (!induction || !isa<IntOp>(loop->step))
    return;

  auto header = loop->header;
  auto preheader = loop->preheader;
  auto phis = header->getPhis();

  Op *splicer = nullptr;
  int vp, vl;
  for (auto phi : phis) {
    if (phi->getOperandCount() != 2)
      continue;

    auto l = Op::getPhiFrom(phi, latch), r = Op::getPhiFrom(phi, preheader);
    if (!l->has<RangeAttr>() || !r->has<RangeAttr>())
      continue;

    auto [a1, a2] = RANGE(l);
    auto [b1, b2] = RANGE(r);
    if (a1 != a2 || b1 != b2)
      continue;

    splicer = phi;
    vl = a1, vp = b1;
    break;
  }
  if (!splicer)
    return;

  // Now copy the loop.
  std::unordered_map<Op*, Op*> cloneMap;
  std::unordered_map<BasicBlock*, BasicBlock*> rewireMap;

  auto region = header->getParent();
  for (auto x : loop->getBlocks())
    rewireMap[x] = region->insert(header);

  auto newpreheader = region->insert(header);

  // The new preheader should connect to header.
  Builder builder;
  builder.setToBlockEnd(newpreheader);
  builder.create<GotoOp>({ new TargetAttr(header) });

  // Shallow copy ops.
  for (auto [k, v] : rewireMap) {
    builder.setToBlockEnd(v);
    for (auto op : k->getOps()) {
      Op *cloned = builder.copy(op);
      cloneMap[op] = cloned;
    }
  }

  // Rewire operands.
  for (auto &[old, cloned] : cloneMap) {
    for (int i = 0; i < old->getOperandCount(); i++) {
      if (cloneMap.count(old->DEF(i)))
        cloned->setOperand(i, cloneMap[old->DEF(i)]);
    }
  }

  // Rewire blocks.
  for (auto [_, v] : rewireMap) {
    auto term = v->getLastOp();
    if (auto attr = term->find<TargetAttr>(); attr && rewireMap.count(attr->bb))
      attr->bb = rewireMap[attr->bb];
    if (auto attr = term->find<ElseAttr>(); attr && rewireMap.count(attr->bb))
      attr->bb = rewireMap[attr->bb];
  }

  // In the inner loops, the phis must be fixed.
  for (auto [_, v] : rewireMap) {
    auto phis = v->getPhis();
    for (auto phi : phis) {
      for (auto attr : phi->getAttrs()) {
        if (!isa<FromAttr>(attr))
          continue;
        auto &from = FROM(attr);
        if (rewireMap.count(from))
          from = rewireMap[from];
      }
    }
  }

  // The new latch should now get to the new preheader.
  auto term = rewireMap[latch]->getLastOp();
  builder.replace<GotoOp>(term, { new TargetAttr(newpreheader) });

  // The preheader should go to the cloned header.
  builder.replace<GotoOp>(preheader->getLastOp(), { new TargetAttr(rewireMap[header]) });

  // The phi itself can be replaced with an IntOp.
  builder.setBeforeOp(preheader->getLastOp());
  auto pi = builder.create<IntOp>({ new IntAttr(vp) });
  cloneMap[splicer]->replaceAllUsesWith(pi);
  cloneMap[splicer]->erase();

  auto li = builder.create<IntOp>({ new IntAttr(vl) });
  splicer->replaceAllUsesWith(li);
  splicer->erase();

  // Every phi of the new header should be replaced by the value from old preheader.
  phis = rewireMap[header]->getPhis();
  for (auto phi : phis) {
    phi->replaceAllUsesWith(Op::getPhiFrom(phi, preheader));
    phi->erase();
  }

  // Every phi of the old header should be rewired to the new preheader.
  phis = header->getPhis();
  for (auto phi : phis) {
    // The value of the old header's phi should be rewired from the new latch.
    phi->replaceOperand(Op::getPhiFrom(phi, preheader), cloneMap[Op::getPhiFrom(phi, latch)]);
    for (auto attr : phi->getAttrs()) {
      if (isa<FromAttr>(attr) && FROM(attr) == preheader)
        FROM(attr) = newpreheader;
    }
  }
}

void Splice::run() {
  LoopAnalysis analysis(module);
  analysis.run();
  auto forests = analysis.getResult();

  Range(module).run();
  for (const auto &[func, forest] : forests) {
    for (auto loop : forest.getLoops())
      runImpl(loop);
  }

  auto funcs = collectFuncs();
  for (auto func : funcs) {
    auto region = func->getRegion();
    removeRange(region);
  }
}
