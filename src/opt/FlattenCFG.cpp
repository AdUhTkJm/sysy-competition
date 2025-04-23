#include "Passes.h"
#include "../codegen/CodeGen.h"
#include "../codegen/Attrs.h"
#include <vector>

using namespace sys;

void handleIf(IfOp *x) {
  Builder builder;

  auto bb = x->getParent();
  auto region = bb->getParent();

  // Create a basic block after IfOp. Copy everything after If there.
  auto beforeIf = bb;
  bb = region->insertAfter(bb);
  beforeIf->splitOpsAfter(bb, x);
  
  // Now we're inserting things between `beforeIf` and `bb`. This preserves existing jumps.
  // Move all blocks in thenRegion after the new basic block.
  auto thenRegion = x->getRegion(0);
  auto [thenFirst, thenFinal] = thenRegion->moveTo(beforeIf);
  auto final = thenFinal;

  // This IfOp has an elseRegion. Repeat the process.
  bool hasElse = x->getRegions().size() > 1;
  if (hasElse) {
    auto elseRegion = x->getRegion(1);
    auto [elseFirst, elseFinal] = elseRegion->moveTo(thenFinal);
    final = elseFinal;

    builder.setToBlockEnd(beforeIf);
    builder.create<BranchOp>({ x->getOperand() },{
      new TargetAttr(thenFirst),
      new ElseAttr(elseFirst)
    });
  }

  // The final block of thenRegion must connect to a "end" block.
  auto end = region->insertAfter(final);
  builder.setToBlockEnd(final);
  builder.create<GotoOp>({
    new TargetAttr(end)
  });

  if (hasElse) {
    builder.setToBlockEnd(thenFinal);
    builder.create<GotoOp>({
      new TargetAttr(end)
    });
  } else {
    builder.setToBlockEnd(beforeIf);
    builder.create<BranchOp>({ x->getOperand() }, {
      new TargetAttr(thenFirst),
      new ElseAttr(end)
    });
  }

  x->erase();
}

void handleWhile(WhileOp *x) {
  Builder builder;

  auto bb = x->getParent();
  auto region = bb->getParent();

  // Create a basic block after WhileOp. Copy everything after it there.
  auto beforeWhile = bb;
  bb = region->insertAfter(bb);
  beforeWhile->splitOpsAfter(bb, x);

  // Move blocks and add an end block.
  auto beforeRegion = x->getRegion(0);
  auto [beforeFirst, beforeFinal] = beforeRegion->moveTo(beforeWhile);

  auto afterRegion = x->getRegion(1);
  auto [afterFirst, afterFinal] = afterRegion->moveTo(beforeFinal);

  auto end = region->insertAfter(afterFinal);

  // Turn the final ProceedOp of beforeRegion into a conditional jump.
  auto op = cast<ProceedOp>(beforeFinal->getLastOp());
  Value condition = op->getOperand();
  builder.setBeforeOp(op);
  builder.create<BranchOp>({ condition }, {
    new TargetAttr(afterFirst),
    new ElseAttr(end)
  });
  op->erase();

  // Append a jump to the beginning for afterRegion.
  builder.setToBlockEnd(afterFinal);
  builder.create<GotoOp>({ new TargetAttr(beforeFirst) });

  // Erase the now-empty WhileOp.
  x->erase();
}

bool isTerminator(Op *op) {
  return isa<BranchOp>(op) || isa<GotoOp>(op) || isa<ReturnOp>(op);
}

void tidy(FuncOp *func) {
  Builder builder;
  auto body = func->getRegion();

  // Get a terminator for basic blocks.
  for (auto it = body->begin(); it != body->end(); ++it) {
    auto bb = *it;
    auto next = it; ++next;
    if (bb->getOps().size() == 0 || !isTerminator(bb->getLastOp())) {
      builder.setToBlockEnd(bb);
      builder.create<GotoOp>({ new TargetAttr(*next) });
    }
  }

  body->updatePreds();

  // If a basic block has only a single terminator, try to inline it.
  std::map<BasicBlock*, BasicBlock*> inliner;
  for (auto bb : body->getBlocks()) {
    if (bb->getOps().size() != 1 || !isa<GotoOp>(bb->getLastOp()))
      continue;

    auto last = bb->getLastOp();
    auto target = last->getAttr<TargetAttr>();
    inliner[bb] = target->bb;
  }

  // Apply inliner.
  auto update = [&](BasicBlock *&from) {
    while (inliner.count(from))
      from = inliner[from];
  };

  for (auto bb : body->getBlocks()) {
    auto last = bb->getLastOp();
    if (last->hasAttr<TargetAttr>()) {
      auto target = last->getAttr<TargetAttr>();
      update(target->bb);
    }

    if (last->hasAttr<ElseAttr>()) {
      auto ifnot = last->getAttr<ElseAttr>();
      update(ifnot->bb);
    }
  }

  // Recalculate preds after change.
  body->updatePreds();

  // Remove empty blocks.
  for (auto [k, v] : inliner)
    k->erase();
}

void FlattenCFG::run() {
  auto ifs = module->findAll<IfOp>();
  for (auto x : ifs)
    handleIf(x);
  
  auto whiles = module->findAll<WhileOp>();
  for (auto x : whiles)
    handleWhile(x);

  // Now everything inside functions have been flattened.
  // Tidy up the basic blocks:
  //    1) all basic blocks must have a terminator;
  //    2) empty basic blocks are eliminated;
  //    3) calculate `pred`.
  auto functions = module->findAll<FuncOp>();
  for (auto x : functions)
    tidy(x);
}
