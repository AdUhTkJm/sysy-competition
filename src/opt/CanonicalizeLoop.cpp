#include "LoopPasses.h"

using namespace sys;

// Each basic block might require a different value (because they are dominated by different exits).
// This function finds which value is the required one.
Op *getValueFor(BasicBlock *bb, LoopInfo *info, std::map<BasicBlock*, Op*> &phiMap) {
  if (phiMap.count(bb))
    return phiMap[bb];
  
  // If the block is immediate-dominated by a block inside loop, 
  // then it must not be dominated by the exit block (note that exit block does not lie in a loop).
  // Otherwise we aren't sure; we can only recurse
  if (!info->contains(bb->getIdom())) {
    auto op = getValueFor(bb->getIdom(), info, phiMap);
    return phiMap[bb] = op;
  }

  // Now we know it isn't dominated by any exit block. This implies a phi.
  Builder builder;
  builder.setToBlockStart(bb);
  auto phi = builder.create<PhiOp>();
  for (auto pred : bb->getPreds()) {
    phi->pushOperand(getValueFor(pred, info, phiMap));
    phi->add<FromAttr>(pred);
  }
  return phiMap[bb] = phi;
}

void CanonicalizeLoop::canonicalize(LoopInfo *loop) {
  for (auto subloop : loop->getSubloops())
    canonicalize(subloop);

  Builder builder;

  // Convert to LCSSA.
  for (auto bb : loop->getBlocks()) {
    for (auto &op : bb->getOps()) {
      bool hasOutsideUses = false;
      for (auto use : op->getUses()) {
        if (!loop->contains(use->getParent())) {
          hasOutsideUses = true;
          break;
        }
      }
      if (!hasOutsideUses)
        continue;

      const auto &exits = loop->getExits();
      if (!exits.size())
        continue;

      std::map<BasicBlock*, Op*> phiMap;
      std::set<Op*> produced;

      // Insert phi that comes from all exitING nodes.
      for (auto exit : exits) {
        if (phiMap.count(exit) || !exit->dominatedBy(bb))
          continue;

        builder.setToBlockStart(exit);
        auto phi = builder.create<PhiOp>();
        for (auto pred : exit->getPreds()) {
          if (loop->contains(pred)) {
            phi->pushOperand(op);
            phi->add<FromAttr>(pred);
          }
        }
        phiMap[exit] = phi;
        produced.insert(phi);
      }
      
      // Replace uses outside of the loop with phi.
      auto uses = op->getUses();
      for (auto use : uses) {
        auto parent = use->getParent();
        // Phi should be treated as from the place where that operands comes from.
        // See 28.sy; consider:
        //
        // bb24:
        //   %69 = phi %77 <from = bb25> ...
        // bb27:
        //   %77 = phi %69 <from = bb24> ...
        //   goto <bb25>
        // bb25:
        //   goto <bb24>
        //
        // When we handle the loop { 27, 25 }, we need to treat `phi %77` as coming from bb25 rather than bb24.
        // It's sensible because the phi is actually on the edge.
        if (isa<PhiOp>(use)) {
          for (size_t i = 0; i < use->getOperands().size(); i++) {
            if (use->getOperand(i).defining == op) {
              parent = cast<FromAttr>(use->getAttrs()[i])->bb;
              break;
            }
          }
        }

        // Don't replace the phi we've just created.
        if (loop->contains(parent) || produced.count(use))
          continue;

        auto replace = getValueFor(parent, loop, phiMap);
        use->replaceOperand(op, replace);
      }
    }
  }
}

void CanonicalizeLoop::runImpl(Region *region, LoopForest forest) {
  region->updateDoms();
  // As `canonicalize` calls subloops recursively, 
  // we only need to call it on all top-level loops.
  for (auto loop : forest.getLoops()) {
    if (!loop->getParent())
      canonicalize(loop);
  }
}

void CanonicalizeLoop::run() {
  Builder builder;
  LoopAnalysis loop(module);
  loop.run();
  auto info = loop.getResult();

  auto funcs = collectFuncs();

  // Make sure each loop have a single preheader.
  for (auto func : funcs) {
    LoopForest forest = info[func];

    for (auto loop : forest.getLoops()) {
      auto header = loop->getHeader();
      if (!loop->getPreheader()) {
        const auto &preds = header->getPreds();
        auto region = header->getParent();
        auto preheader = region->insert(header);

        // Reconnect the predecessors to jump to preheader instead.
        for (auto pred : preds) {
          if (loop->contains(pred))
            continue;

          auto term = pred->getLastOp();
          if (term->has<TargetAttr>() && TARGET(term) == header)
            TARGET(term) = preheader;
          if (term->has<ElseAttr>() && ELSE(term) == header)
            ELSE(term) = preheader;
        }

        // Rewire non-backedge phi's at header to preheader.
        auto phis = header->getPhis();
        for (auto phi : phis) {
          std::vector<std::pair<Op*, BasicBlock*>> forwarded, preserved;
          for (size_t i = 0; i < phi->getOperands().size(); i++) {
            auto from = cast<FromAttr>(phi->getAttrs()[i])->bb;
            if (!loop->getLatches().count(from))
              forwarded.push_back({ phi->getOperand(i).defining, from });
            else
              preserved.push_back({ phi->getOperand(i).defining, from });
          }

          // These form a new phi at the preheader.
          if (forwarded.size()) {
            builder.setToBlockEnd(preheader);
            Op *newPhi = builder.create<PhiOp>();
            for (auto [def, from] : forwarded) {
              newPhi->pushOperand(def);
              newPhi->add<FromAttr>(from);
            }

            // Remove all forwarded operands, and push a { newPhi, preheader } pair.
            phi->removeAllOperands();
            phi->removeAllAttributes();
            
            for (auto [def, from] : preserved) {
              phi->pushOperand(def);
              phi->add<FromAttr>(from);
            }
            phi->pushOperand(newPhi);
            phi->add<FromAttr>(preheader);
          }
        }

        // Wire preheader to the header.
        builder.setToBlockEnd(preheader);
        builder.create<GotoOp>({ new TargetAttr(header) });
      }
    }
  }

  loop.run();
  info = loop.getResult();
  // Do LCSSA on each function.
  for (auto func : funcs)
    runImpl(func->getRegion(), info[func]);
}
