#include "Passes.h"
#include "../codegen/CodeGen.h"
#include "../codegen/Attrs.h"
#include <iostream>

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

void FlattenCFG::run() {
  auto ifs = module->findAll<IfOp>();
  for (auto x : ifs)
    handleIf(x);

  module->dump(std::cerr);
  
  auto whiles = module->findAll<WhileOp>();
  for (auto x : whiles)
    handleWhile(x);

  // Now everything inside functions have been flattened.
  // Tidy up the basic blocks:
  //    1) all basic blocks must have a terminator;
  //    2) empty basic blocks are eliminated;
  //    3) calculate `pred`.
}
