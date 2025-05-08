#include "LoopPasses.h"
#include "../utils/Matcher.h"

using namespace sys;

void postorder(LoopInfo *loop, std::vector<LoopInfo*> &loops) {
  for (auto subloop : loop->getSubloops())
    loops.push_back(subloop);
  loops.push_back(loop);
}

void LoopRotate::runImpl(LoopInfo *info) {
  if (!info->getInduction())
    return;
  if (info->getExits().size() != 1)
    return;

  auto exit = *info->getExits().begin();

  auto induction = info->getInduction();
  auto header = info->getHeader();
  // We only rotate canonicalized loop in form `for (int i = %0; i < 'a; i += 'b)`
  // That's because it's difficult (taking me >5h but still unable to implement) to hoist an arbitrary condition out of loop.
  // Check header condition for this.
  auto term = header->getLastOp();
  Rule br("(br (lt i 'a))");
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

  builder.setBeforeOp(preterm);
  auto upper = builder.create<IntOp>({ new IntAttr(V(br.extract("'a"))) });
  auto lt = builder.create<LtOp>({ (Value) info->getStart(), upper });
  builder.replace<BranchOp>(preterm, { lt }, { new TargetAttr(header), new ElseAttr(exit) });

  // Replace the branch at header with a goto.
  auto target = TARGET(term);
  auto cond = term->getOperand();
  builder.replace<GotoOp>(term, { new TargetAttr(target) });

  // Replace the latch's terminator with a branch.
  // This time we should compare the increased induction variable with the upper bound.
  // Don't worry, GVN will handle all these extra things.
  builder.setBeforeOp(latchterm);
  auto step = builder.create<IntOp>({ new IntAttr(info->getStep()) });
  auto add = builder.create<AddIOp>({ induction, step });
  auto lt2 = builder.create<LtOp>({ add, upper });
  builder.replace<BranchOp>(latchterm, { lt2 }, { new TargetAttr(header), new ElseAttr(exit) });

  // Fix phi nodes at exit.
  auto phis = exit->getPhis();
  for (auto phi : phis) {
    const auto &ops = phi->getOperands();
    const auto &attrs = phi->getAttrs();
    for (size_t i = 0; i < ops.size(); i++) {
      auto &from = cast<FromAttr>(attrs[i])->bb;
      if (from == header)
        from = latch;
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
          
          for (auto [def, from] : preserved) {
            phi->pushOperand(def);
            phi->add<FromAttr>(from);
          }
          phi->pushOperand(newPhi);
          phi->add<FromAttr>(latch);
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
