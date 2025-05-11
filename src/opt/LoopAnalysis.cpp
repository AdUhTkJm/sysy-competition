#include "LoopPasses.h"
#include "../utils/Matcher.h"

using namespace sys;

LoopForest LoopAnalysis::runImpl(Region *region) {
  region->updateDoms();

  LoopForest forest;
  
  // Collect all blocks and latches in a loop.
  for (auto bb : region->getBlocks()) {
    for (auto succ : bb->getSuccs()) {
      if (!bb->dominatedBy(succ))
        continue;
      
      // `bb` is dominated by `succ`; this is a backedge.

      // The header is already seen (as the loop has multiple latches).
      // Skip it, but we have to update the loop info accordingly.
      LoopInfo *info;
      BasicBlock *header;
      if (forest.loopMap.count(succ)) {
        info = forest.loopMap[succ];
        info->bbs.insert(bb);
        header = info->header;
      } else {
        info = new LoopInfo;
        header = succ;
        info->header = header;
        info->bbs = { header, bb };
      }

      info->latches.insert(bb);

      std::vector<BasicBlock*> worklist { bb };
      while (!worklist.empty()) {
        auto back = worklist.back();
        worklist.pop_back();
        // Don't traverse beyond the header.
        if (back == header)
          continue;
        
        for (auto pred : back->getPreds()) {
          if (!info->bbs.count(pred)) {
            info->bbs.insert(pred);
            worklist.push_back(pred);
          }
        }
      }

      forest.loopMap[succ] = info;
    }
  }

  for (auto [k, v] : forest.loopMap)
    forest.loops.push_back(v);

  // Update more information of the loops: exit(ing)s, parent and preheader.
  for (auto info : forest.loops) {
    auto header = info->header;

    BasicBlock *preheader = nullptr;
    // Find preheader if there exists one.
    for (auto pred : header->getPreds()) {
      if (info->latches.count(pred))
        continue;
      if (preheader) {
        // At least 2 blocks can jump to `header`. Fail.
        preheader = nullptr;
        break;
      }
      preheader = pred;
    }
    // Preheader must also have a single edge to header.
    if (preheader && preheader->getSuccs().size() == 1)
      info->preheader = preheader;
    
    // Find exit and exiting blocks.
    for (auto loopbb : info->bbs) {
      bool exiting = false;
      for (auto succ : loopbb->getSuccs()) {
        if (!info->contains(succ)) {
          exiting = true;
          info->exits.insert(succ);
          break;
        }
      }
      if (exiting)
        info->exitings.insert(loopbb);
    }

    // Find if itself is nested over some other loop.
    std::vector<BasicBlock*> candidates;
    for (auto [head, info] : forest.loopMap) {
      if (info->contains(header) && head != header)
        candidates.push_back(head);
    }

    // Find the deepest nest: the one contained in all other loops.
    for (auto x : candidates) {
      bool direct = true;
      for (auto y : candidates) {
        if (x != y && !x->dominatedBy(y)) {
          direct = false;
          break;
        }
      }
      if (direct) {
        // Now `x` is the direct parent of the loop `info`.
        LoopInfo *parentInfo = forest.getInfo(x);
        info->parent = parentInfo;
        parentInfo->subloops.push_back(info);
        break;
      }
    }
  }

  // Try to find the induction variable.
  Rule addi("(add x 'a)");
  Rule br("(br (lt x y))");
  Rule brRotated("(br (lt (add x 'a) y))");
  for (auto loop : forest.getLoops()) {
    auto header = loop->getHeader();
    auto phis = header->getPhis();
    if (!phis.size() || loop->getLatches().size() != 1)
      continue;

    auto preheader = loop->getPreheader();
    if (!preheader)
      continue;

    auto latch = loop->getLatch();
    for (auto phi : phis) {
      const auto &ops = phi->getOperands();
      const auto &attrs = phi->getAttrs();
      if (ops.size() != 2)
        continue;

      auto bb1 = cast<FromAttr>(attrs[0])->bb;
      auto bb2 = cast<FromAttr>(attrs[1])->bb;
      auto def1 = ops[0].defining;
      auto def2 = ops[1].defining;

      if (bb1 == latch && bb2 == preheader) {
        std::swap(bb1, bb2);
        std::swap(def1, def2);
      }
      if (bb1 == preheader && bb2 == latch) {
        // Now this is a candidate of induction variable.
        // See if `def2` is of form `%phi + 'a`.

        // addi: (add x 'a)
        if (addi.match(def2, { { "x", phi } })) {
          // This should be in latch.
          if (def2->getParent() != latch)
            continue;

          auto step = addi.extract("'a");

          // If, after this `add` there's still something else (other than branch), 
          // then we change the `def2`s there to use `phi + 'a` instead.
          Builder builder;
          for (Op *op = def2->nextOp(); op != latch->getLastOp(); op = op->nextOp()) {
            const auto &ops = op->getOperands();
            for (size_t i = 0; i < ops.size(); i++) {
              auto def = ops[i].defining;
              if (def == def2) {
                builder.setBeforeOp(op);
                auto a = builder.create<IntOp>({ new IntAttr(V(step)) });
                auto addi = builder.create<AddIOp>({ phi, a });
                op->setOperand(i, addi);
              }
            }
          }
          // Then we sink `def2` to the bottom.
          def2->moveBefore(latch->getLastOp());

          // OK, now this is definitely an induction variable.
          loop->induction = phi;
          loop->start = def1;
          loop->step = V(step);

          // Try to identify the stop condition by looking at header.
          auto term = header->getLastOp();
          if (isa<GotoOp>(term)) {
            // Already rotated. Check latch instead.
            // brRotated: (br (lt (add x 'a) y))
            term = latch->getLastOp();
            if (!brRotated.match(term,  { { "x", loop->induction } }))
              break;

            loop->stop = brRotated.extract("y");
            break;
          }

          // br: (br (lt x y))
          if (!br.match(term, { { "x", loop->induction } }))
            break;

          loop->stop = br.extract("y");
          break;
        }
      }
    }
  }

  return forest;
}

void LoopAnalysis::run() {
  auto funcs = collectFuncs();
  for (auto func : funcs)
    info[func] = runImpl(func->getRegion());
}
