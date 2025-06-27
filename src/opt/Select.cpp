#include "Passes.h"

using namespace sys;

namespace {

#define ALLOWED(Ty) || isa<Ty>(op)
bool hoistable(Op *op) {
  return isa<AddIOp>(op)
    ALLOWED(IntOp)
    ALLOWED(AddLOp)
    ALLOWED(SubIOp)
    ALLOWED(OrIOp)
    ALLOWED(AndIOp)
    ALLOWED(XorIOp)
    ALLOWED(EqOp)
    ALLOWED(LeOp)
    ALLOWED(LtOp)
    ALLOWED(NeOp)
  ;
}

bool identical(Op *a, Op *b) {
  if (a->opid != b->opid || a->getOperandCount() != b->getOperandCount())
    return false;

  // Avoid infinite loops.
  if (isa<PhiOp>(a))
    return false;

  if (isa<IntOp>(a))
    return V(a) == V(b);
  
  for (int i = 0; i < a->getOperandCount(); i++) {
    if (!identical(a->DEF(i), b->DEF(i)))
      return false;
  }
  return true;
}

}

std::map<std::string, int> Select::stats() {
  return {
    { "raised-selects", raised }
  };
}

void Select::run() {
  Builder builder;
  auto phis = module->findAll<PhiOp>();

  // Hoist identical ops out of an if.
  for (auto phi : phis) {
    if (phi->getOperandCount() != 2)
      continue;

    const auto &attrs = phi->getAttrs();
    auto bb1 = FROM(attrs[0]), bb2 = FROM(attrs[1]);

    // Check if their unique predecessors are the same.
    if (bb1->preds.size() != 1 || bb2->preds.size() != 1)
      continue;
    auto pred1 = *bb1->preds.begin(), pred2 = *bb2->preds.begin();
    if (pred1 != pred2)
      continue;

    auto pred = pred1;
    auto term = pred->getLastOp();
    
    // Now check if both bb1 and bb2 have identical initial op sequence.
    auto r1 = bb1->getFirstOp(), r2 = bb2->getFirstOp();
    while (hoistable(r1) && identical(r1, r2)) {
      auto x1 = r1->nextOp(), x2 = r2->nextOp();
      
      r1->moveBefore(term);
      r2->replaceAllUsesWith(r1);
      r2->erase();

      r1 = x1, r2 = x2;
    }
  }

  for (auto phi : phis) {
    // If the phi has two empty blocks as predecessors, then it is a `select`.
    if (phi->getOperandCount() != 2)
      continue;

    const auto &attrs = phi->getAttrs();
    auto bb1 = FROM(attrs[0]), bb2 = FROM(attrs[1]);
    if (bb1->getOpCount() != 1 || bb2->getOpCount() != 1)
      continue;

    // TODO: check for a single block that have a single non-terminator.

    // Check if their unique predecessors are the same.
    if (bb1->preds.size() != 1 || bb2->preds.size() != 1)
      continue;
    auto pred1 = *bb1->preds.begin(), pred2 = *bb2->preds.begin();
    if (pred1 != pred2)
      continue;

    auto pred = pred1;
    // We know the last op of `pred` is a branch op,
    // which is the condition of the select.

    auto term = pred->getLastOp();
    auto cond = term->getOperand(0);

    // Replace the phi with a select.
    if (phi->DEF(0)->getResultType() != Value::i32)
      continue;

    auto _true = TARGET(term), _false = ELSE(term);
    auto select = builder.replace<SelectOp>(phi, { cond, Op::getPhiFrom(phi, _true), Op::getPhiFrom(phi, _false) });
    
    // Move it below all the phis in the same block.
    auto parent = select->getParent();
    auto insert = nonphi(parent);
    select->moveBefore(insert);

    // Replace the branch of `pred` to connect to `parent` instead.
    builder.replace<GotoOp>(term, { new TargetAttr(parent) });

    // The empty blocks `bb1` and `bb2` are dead. Erase them.
    bb1->forceErase();
    bb2->forceErase();
  }

  // Moreover, raise the following to select:
  //   br %1 <bb1>, <bb2>
  // bb1:
  //   %2 = <relocatable>
  //   goto <bb3>
  // bb2:
  //   goto <bb3>
  // bb3:
  //   phi = [%2, bb1], [%0, bb2]
  //
  // It should become
  //   select %1 %2 %0
  phis = module->findAll<PhiOp>();

  for (auto phi : phis) {
    if (phi->getOperandCount() != 2)
      continue;

    const auto &attrs = phi->getAttrs();
    auto bb1 = FROM(attrs[0]), bb2 = FROM(attrs[1]);
    if (bb1->getOpCount() == 1 && bb2->getOpCount() != 1)
      std::swap(bb1, bb2);
    if (bb2->getOpCount() != 1 || bb1->getOpCount() != 2)
      continue;

    // The `%2` above is the first op in `bb1`.
    auto v2 = bb1->getFirstOp();
    // It has to be relocatable, and has to be "worth it",
    // because we're pulling it out of an if.
    // We use a whitelist.
    if (!hoistable(v2))
      continue;

    // Check if their unique predecessors are the same.
    if (bb1->preds.size() != 1 || bb2->preds.size() != 1)
      continue;
    auto pred1 = *bb1->preds.begin(), pred2 = *bb2->preds.begin();
    if (pred1 != pred2)
      continue;

    auto pred = pred1;
    
    // The rest is quite similar.
    auto term = pred->getLastOp();
    auto cond = term->getOperand(0);

    // Replace the phi with a select.
    if (phi->DEF(0)->getResultType() != Value::i32)
      continue;

    v2->moveBefore(pred->getLastOp());

    auto _true = TARGET(term), _false = ELSE(term);
    auto select = builder.replace<SelectOp>(phi, { cond, Op::getPhiFrom(phi, _true), Op::getPhiFrom(phi, _false) });
    
    // Move it below all the phis in the same block.
    auto parent = select->getParent();
    auto insert = nonphi(parent);
    select->moveBefore(insert);

    // Replace the branch of `pred` to connect to `parent` instead.
    builder.replace<GotoOp>(term, { new TargetAttr(parent) });

    // The empty blocks `bb1` and `bb2` are dead. Erase them.
    bb1->forceErase();
    bb2->forceErase();
  }
}
