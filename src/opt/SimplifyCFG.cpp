#include "CleanupPasses.h"

using namespace sys;

std::map<std::string, int> SimplifyCFG::stats() {
  return {
    { "inlined-blocks", inlined },
  };
}

void SimplifyCFG::runImpl(Region *region) {
  region->updatePreds();
  bool changed;
  do {
    changed = false;
    const auto &bbs = region->getBlocks();
    for (auto bb : bbs) {
      if (bb->succs.size() != 1)
        continue;

      auto succ = *bb->succs.begin();
      if (succ->preds.size() != 1)
        continue;

      // Now we can safely inline `succ` into `bb`.
      // If `succ` have phi, then it must be single-operand. Inline them.
      auto phis = succ->getPhis();
      for (auto phi : phis) {
        auto def = phi->getOperand().defining;
        phi->replaceAllUsesWith(def);
        phi->erase();
      }

      // Remove the jump to `succ`.
      bb->getLastOp()->erase();

      // Then move all instruction in `succ` to `bb`.
      auto ops = succ->getOps();
      for (auto op : ops)
        op->moveToEnd(bb);

      // All successors of `succ` now have pred `bb`.
      // `bb` also regard them as successors.
      for (auto s : succ->succs) {
        s->preds.erase(succ);
        s->preds.insert(bb);
        bb->succs.insert(s);
      }
      // Don't forget to remove `succ` from the successors of `bb`.
      bb->succs.erase(succ);

      // The phis at the beginning of some successor need to refer to `bb`.
      for (auto s : succ->succs) {
        auto phis = s->getPhis();
        for (auto phi : phis) {
          const auto &attrs = phi->getAttrs();

          for (int i = 0; i < attrs.size(); i++) {
            auto &from = FROM(attrs[i]);
            if (from == succ)
              from = bb;
          }
        }
      }

      succ->forceErase();
      inlined++;
      changed = true;
      break;
    }
  } while (changed);
}

void SimplifyCFG::run() {
  auto funcs = collectFuncs();
  for (auto func : funcs)
    runImpl(func->getRegion());

  // Simplify the following construction:
  //   if (x) ...; if (!x) ...
  // becomes a single if-else.
  //
  // In IR, it should be like
  //
  // bb1:
  //   br %1, <bb2>, <exit>
  // bb2:
  //   goto <exit>
  // exit:
  //   br (not %1), <bb3>, <exit2>
  // bb3:
  //   goto <exit2>
  // exit2:
  //   ...
  //
  // As multiple linear goto's have been combined, there's less risk of missed optimization.
  // To fold it:
  //
  // bb1:
  //   br %1, <bb2>, <bb3>
  // bb2:
  //   goto <exit2>
  // exit (now dead):
  //   br (not %1), <bb3>, <exit2>
  // bb3:
  //   goto <exit2>
  // exit2:
  //   ...
  // Note that `exit` must only contain a NotOp and a branch.
}
