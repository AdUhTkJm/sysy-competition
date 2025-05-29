#include "LowerPasses.h"
#include "../codegen/CodeGen.h"
#include "../codegen/Attrs.h"
#include <vector>

using namespace sys;

static void handleIf(Op *x) {
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

static void handleWhile(Op *x) {
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

  auto unusedBB = region->insertAfter(end);

  // Replace all breaks and continues.
  // Note that we might encounter nested WhileOps. The breaks and continues would be naturally ignored. 
  // Early returns should also be handled.
  for (auto bb = beforeFirst; bb != end; bb = bb->nextBlock()) {
    std::vector<Op*> disrupters;
    for (auto op : bb->getOps()) {
      if (isa<BreakOp>(op) || isa<ContinueOp>(op) || isa<ReturnOp>(op))
        disrupters.push_back(op);
    }

    std::set<Op*> skipped;

    for (auto op : disrupters) {
      if (skipped.count(op))
        continue;
      
      auto bb = op->getParent();
      // If it's not at the end of the basic block, then all Ops after it are useless.
      // This also moves `op` itself, so we move it back.
      bb->splitOpsAfter(unusedBB, op);
      op->moveToEnd(bb);
      for (auto unused : unusedBB->getOps())
        unused->removeAllOperands();
      auto copy = unusedBB->getOps();
      for (auto unused : copy) {
        // It is possible that "unused" itself is also in disrupters.
        // In that case we must skip it in the following process.
        if (isa<BreakOp>(op) || isa<ContinueOp>(op) || isa<ReturnOp>(op))
          skipped.insert(unused);

        unused->erase();
      }
      
      if (isa<BreakOp>(op))
        builder.replace<GotoOp>(op, { new TargetAttr(end) });
      else if (isa<ContinueOp>(op))
        builder.replace<GotoOp>(op, { new TargetAttr(beforeFirst) });
    }
  }

  unusedBB->erase();

  // Erase the now-empty WhileOp.
  x->erase();
}

static bool isTerminator(Op *op) {
  return isa<BranchOp>(op) || isa<GotoOp>(op) || isa<ReturnOp>(op);
}

void tidy(FuncOp *func) {
  Builder builder;
  auto body = func->getRegion();

  // If the last Op isn't a `return`, then supply a `return`.
  auto last = body->getLastBlock();
  if (last->getOpCount() == 0 || !isa<ReturnOp>(last->getLastOp())) {
    builder.setToBlockEnd(body->getLastBlock());
    builder.create<ReturnOp>();
  }

  // Remove redundant terminators.
  // These might be inserted by flattening when terminators are already present.
  for (auto bb : body->getBlocks()) {
    Op *term = nullptr;
    for (auto op : bb->getOps()) {
      if (isTerminator(op)) {
        term = op;
        break;
      }
    }
    if (!term || term == bb->getLastOp())
      continue;

    std::vector<Op*> remove;
    for (auto op = term->nextOp(); op != bb->getLastOp(); op = op->nextOp()) {
      op->removeAllOperands();
      remove.push_back(op);
    }
    bb->getLastOp()->removeAllOperands();

    for (auto op : remove)
      op->erase();
    bb->getLastOp()->erase();
  }

  // Get a terminator for basic blocks.
  for (auto it = body->begin(); it != body->end(); ++it) {
    auto bb = *it;
    auto next = it; ++next;
    if (bb->getOpCount() == 0 || !isTerminator(bb->getLastOp())) {
      builder.setToBlockEnd(bb);
      builder.create<GotoOp>({ new TargetAttr(*next) });
    }
  }

  body->updatePreds();

  // If a basic block has only a single terminator, try to inline it.
  std::map<BasicBlock*, BasicBlock*> inliner;
  for (auto bb : body->getBlocks()) {
    if (bb->getOpCount() != 1 || !isa<GotoOp>(bb->getLastOp()))
      continue;

    auto last = bb->getLastOp();
    auto target = last->get<TargetAttr>();
    inliner[bb] = target->bb;
  }

  // Apply inliner.
  auto update = [&](BasicBlock *&from) {
    while (inliner.count(from))
      from = inliner[from];
  };

  for (auto bb : body->getBlocks()) {
    auto last = bb->getLastOp();
    if (last->has<TargetAttr>()) {
      auto target = last->get<TargetAttr>();
      update(target->bb);
    }

    if (last->has<ElseAttr>()) {
      auto ifnot = last->get<ElseAttr>();
      update(ifnot->bb);
    }
  }

  // Recalculate preds after change.
  body->updatePreds();

  // Remove inlined blocks.
  for (auto [k, v] : inliner)
    k->erase();
  
  body->updatePreds();

  // If the (newly produced) first block has any preds, then make a new entry block,
  // and move the real "entry ops" (alloca and getarg) to that block.
  if (body->getFirstBlock()->preds.size() >= 1) {
    auto first = body->getFirstBlock();
    auto entry = body->insert(first);
    auto ops = first->getOps();
    for (auto op : ops) {
      if (isa<AllocaOp>(op) || isa<GetArgOp>(op))
        op->moveToEnd(entry);
    }
    // Supply a terminator.
    builder.setToBlockEnd(entry);
    builder.create<GotoOp>({ new TargetAttr(first) });
  }

  body->updatePreds();
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
  auto functions = collectFuncs();
  for (auto x : functions)
    tidy(x);
}
