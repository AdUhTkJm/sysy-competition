#include "ArmLoopPasses.h"

using namespace sys::arm;
using namespace sys;

void PostIncr::runImpl(LoopInfo *info) {
  if (info->latches.size() > 1)
    return;

  auto header = info->header;
  auto latch = info->getLatch();
  auto phis = header->getPhis();

  // Find a series of straight lines from the latch.
  std::vector<BasicBlock*> final;
  auto runner = latch;
  do {
    final.push_back(runner);
    if (runner->preds.size() > 1)
      break;
    runner = *runner->preds.begin();
  } while (runner != header);

  Builder builder;

  for (auto phi : phis) {
    // First ensure this increments by an in-range constant.
    auto latchval = Op::getPhiFrom(phi, latch);
    if (!isa<AddXIOp>(latchval))
      continue;
    auto vi = V(latchval);

    // Out of range.
    if (vi >= 256 || vi < -256)
      continue;

    // Find the last use of this phi, except `latchval`.
    Op *lastuse = nullptr;
    auto lastbb = final.end();

    for (auto use : phi->getUses()) {
      if (use == latchval)
        continue;

      auto parent = use->getParent();
      if (!info->contains(parent))
        continue;
      auto it = std::find(final.begin(), final.end(), parent);
      if (it == final.end())
        continue;

      if (!lastuse || it < lastbb) {
        // Either that's the first use, or found a later use.
        lastuse = use;
        lastbb = it;
      } else if (it == lastbb) {
        // The same block; find which's later.
        bool later = use->atBack();
        if (!later) {
          for (auto p = lastuse; !p->atBack(); p = p->nextOp()) {
            if (p == use) {
              later = true;
              break;
            }
          }
        }

        // If the current `use` is later, then update it.
        if (later) {
          lastuse = use;
          lastbb = it;
        }
      }
    }

    if (!lastuse || !lastuse->has<IntAttr>() || V(lastuse) != 0)
      continue;

    builder.setBeforeOp(lastuse);
    Op *replace = nullptr;
    switch (lastuse->opid) {
    case LdrWOp::id:
      replace = builder.create<LdrWPOp>(lastuse->getOperands(), { new IntAttr(vi) });
      break;
    case StrWOp::id:
      replace = builder.create<StrWPOp>(lastuse->getOperands(), { new IntAttr(vi) });
      break;
    }
    if (!replace)
      continue;

    lastuse->replaceAllUsesWith(replace);
    lastuse->erase();

    // Make sure the value of `phi` lives correctly.
    builder.setAfterOp(latchval);
    auto placeholder = builder.create<PlaceHolderOp>({ phi });
    phi->replaceOperand(latchval, placeholder);
    
    // Now `phi`'s value is already increased, so `latchval` is now equal to `phi`.
    latchval->replaceAllUsesWith(phi);
  }
}

void PostIncr::run() {
  LoopAnalysis analysis(module);
  analysis.run();
  auto forests = analysis.getResult();

  for (const auto &[_, forest] : forests) {
    for (auto loop : forest.getLoops()) {
      // Only consider innermost loops.
      if (loop->subloops.size() == 0)
        runImpl(loop);
    }
  }
}
