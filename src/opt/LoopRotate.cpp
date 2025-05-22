#include "LoopPasses.h"
#include "../utils/Matcher.h"

using namespace sys;

static void postorder(LoopInfo *loop, std::vector<LoopInfo*> &loops) {
  for (auto subloop : loop->getSubloops())
    loops.push_back(subloop);
  loops.push_back(loop);
}

std::map<std::string, int> LoopRotate::stats() {
  return {
    { "rotated-loops", rotated }
  };
}

void LoopRotate::runImpl(LoopInfo *info) {
  if (!info->getInduction())
    return;
  if (info->getExits().size() != 1)
    return;

  auto exit = *info->getExits().begin();

  auto induction = info->getInduction();
  auto header = info->getHeader();
  // We only rotate canonicalized loop in form `for (int i = %0; i < x; i += 'b)`
  // Here `x` is an SSA value defined outside loop.
  // Check header condition for this.
  auto term = header->getLastOp();
  Rule br("(br (lt i x))");
  if (!br.match(term, { { "i", induction } }))
    return;
  if (ELSE(term) != exit)
    return;

  auto latch = info->getLatch();
  auto latchterm = latch->getLastOp();
  if (!isa<GotoOp>(latchterm))
    return;

  // Now replace the preheader's condition with (%0 < %1).
  Builder builder;

  auto preheader = info->getPreheader();
  auto preterm = preheader->getLastOp();
  if (!isa<GotoOp>(preterm))
    return;
  
  auto upper = br.extract("x");
  auto upperFrom = upper->getParent();
  if (!upperFrom->dominates(header) || upperFrom == header) {
    if (!isa<IntOp>(upper))
      return;

    // We can hoist that constant out of loop.
    upper->moveBefore(preterm);
  }

  rotated++;

  auto headerPhis = header->getPhis();
  std::map<Op*, Op*> valueMap, initMap;
  // Map each phi to the value from latch.
  // When we update references outside the loop, we'll change phi to that value instead.
  for (auto phi : headerPhis) {
    const auto &ops = phi->getOperands();
    const auto &attrs = phi->getAttrs();
    
    if (attrs.size() < 2)
      continue;
    assert(attrs.size() == 2);
    
    auto bb1 = cast<FromAttr>(attrs[0])->bb;
    if (bb1 == latch) {
      valueMap[phi] = ops[0].defining;
      initMap[phi] = ops[1].defining;
    }

    auto bb2 = cast<FromAttr>(attrs[1])->bb;
    if (bb2 == latch){
      valueMap[phi] = ops[1].defining;
      initMap[phi] = ops[0].defining;
    }
  }

  builder.setBeforeOp(preterm);
  auto lt = builder.create<LtOp>({ (Value) info->getStart(), upper });
  builder.replace<BranchOp>(preterm, { lt }, { new TargetAttr(header), new ElseAttr(exit) });

  // Replace the branch at header with a goto.
  auto target = TARGET(term);
  auto cond = term->getOperand();
  builder.replace<GotoOp>(term, { new TargetAttr(target) });

  // Replace the latch's terminator with a branch.
  // This time we should compare the increased induction variable with the upper bound,
  // i.e. the value from latch.
  builder.setBeforeOp(latchterm);
  auto lt2 = builder.create<LtOp>({ valueMap[induction], upper->getResult() });
  builder.replace<BranchOp>(latchterm, { lt2 }, { new TargetAttr(header), new ElseAttr(exit) });

  // Fix phi nodes at exit.
  auto phis = exit->getPhis();
  for (auto phi : phis) {
    const auto &ops = phi->getOperands();
    const auto &attrs = phi->getAttrs();
    for (size_t i = 0; i < ops.size(); i++) {
      auto &from = cast<FromAttr>(attrs[i])->bb;
      auto def = ops[i].defining;
      if (from == header) {
        from = latch;
        if (valueMap.count(def))
          phi->setOperand(i, valueMap[def]);
        if (initMap.count(def)) {
          phi->pushOperand(initMap[def]);
          phi->add<FromAttr>(preheader);
        }
        break;
      }
    }
  }
}

void LoopRotate::run() {
  Builder builder;
  LoopAnalysis loop(module);
  loop.run();
  auto info = loop.getResult();

  auto funcs = collectFuncs();

  // Make sure each loop have a single latch.
  // Similar to Canonicalize::run().
  for (auto func : funcs) {
    LoopForest forest = info[func];

    for (auto loop : forest.getLoops()) {
      auto header = loop->getHeader();
      if (loop->getLatches().size() == 1)
        continue;

      const auto &latches = loop->getLatches();
      auto region = header->getParent();
      auto latch = region->insert(*--latches.end());

      // Reconnect old latches to the new latch.
      for (auto old : latches) {
        auto term = old->getLastOp();
        if (term->has<TargetAttr>() && TARGET(term) == header)
          TARGET(term) = latch;
        if (term->has<ElseAttr>() && ELSE(term) == header)
          ELSE(term) = latch;
      }

      // Rewire backedge phi's at header to latch.
      auto phis = header->getPhis();
      for (auto phi : phis) {
        std::vector<std::pair<Op*, BasicBlock*>> forwarded, preserved;
        for (size_t i = 0; i < phi->getOperands().size(); i++) {
          auto from = cast<FromAttr>(phi->getAttrs()[i])->bb;
          if (latches.count(from))
            forwarded.push_back({ phi->getOperand(i).defining, from });
          else
            preserved.push_back({ phi->getOperand(i).defining, from });
        }

        // These form a new phi at the latch.
        if (forwarded.size()) {
          builder.setToBlockEnd(latch);
          Op *newPhi = builder.create<PhiOp>();
          for (auto [def, from] : forwarded) {
            newPhi->pushOperand(def);
            newPhi->add<FromAttr>(from);
          }

          // Remove all forwarded operands, and push a { newPhi, latch } pair.
          phi->removeAllOperands();
          phi->removeAllAttributes();

          if (!preserved.size()) {
            phi->replaceAllUsesWith(newPhi);
            phi->erase();
          } else {
            for (auto [def, from] : preserved) {
              phi->pushOperand(def);
              phi->add<FromAttr>(from);
            }
            phi->pushOperand(newPhi);
            phi->add<FromAttr>(latch);
          }
        }
      }

      // Wire latch to the header.
      builder.setToBlockEnd(latch);
      builder.create<GotoOp>({ new TargetAttr(header) });
    }
  }

  loop.run();
  info = loop.getResult();
  for (auto func : funcs) {
    const auto &forest = info[func];
    for (auto toploop : forest.getLoops()) {
      std::vector<LoopInfo*> loops;
      postorder(toploop, loops);

      for (auto loop : loops)
        runImpl(loop);
    }
  }
}
