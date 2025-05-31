#include "Passes.h"

using namespace sys;

std::map<std::string, int> Select::stats() {
  return {
    { "raised-selects", raised }
  };
}

void Select::run() {
  Builder builder;
  auto phis = module->findAll<PhiOp>();

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

    // The index of `true` branch.
    auto _true = TARGET(term) != bb1;
    auto select = builder.replace<SelectOp>(phi, { cond, phi->DEF(_true), phi->DEF(1 - _true) });
    
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
