#include "Passes.h"
#include "../codegen/CodeGen.h"
#include "../codegen/Attrs.h"

using namespace sys;

// Moves all blocks in `from` after `insertionPoint`.
// Returns the first and final block.
std::pair<BasicBlock*, BasicBlock*> moveOut(Region *from, BasicBlock *insertionPoint) {
  Builder builder;
  BasicBlock *first = nullptr;
  BasicBlock *prev = insertionPoint;
  BasicBlock *final = nullptr;
  Region *dest = insertionPoint->getParent();

  for (auto block : from->getBlocks()) {
    auto bbNew = dest->insertAfter(prev);
    final = bbNew;
    if (!first)
      first = bbNew;

    block->moveAllOpsTo(bbNew);
    
    builder.setToBlockEnd(prev);
    builder.create<GotoOp>({
      new TargetAttr(bbNew)
    });
  }
  // We will insert an extra GotoOp at the end of insertionPoint.
  insertionPoint->getOps().pop_back();
  return std::make_pair(first, final);
}

void FlattenCFG::run() {
  auto ifs = module->findAll<IfOp>();
  Builder builder;
  for (auto x : ifs) {
    auto bb = x->getParent();

    auto region = bb->getParent();

    // Create a basic block before IfOp. Copy everything before If there.
    auto beforeIf = region->insert(bb);
    for (auto it = bb->begin(); it != bb->end() && *it != x; ) {
      auto advanced = it; ++advanced;
      (*it)->moveToEnd(beforeIf);
      it = advanced;
    }
    
    auto thenRegion = x->getRegion(0);
    auto [thenFirst, thenFinal] = moveOut(thenRegion, beforeIf);
    auto final = thenFinal;

    // This IfOp has an elseRegion. Repeat the process.
    bool hasElse = x->getRegions().size() > 1;
    if (hasElse) {
      auto elseRegion = x->getRegion(1);
      auto [elseFirst, elseFinal] = moveOut(elseRegion, thenFinal);
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
}
