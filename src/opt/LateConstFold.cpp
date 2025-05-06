#include "Passes.h"

using namespace sys;

#define INT(op) (isa<IntOp>(op))

std::map<std::string, int> LateConstFold::stats() {
  return {
    { "folded-ops", foldedTotal }
  };
}

int LateConstFold::foldImpl() {
  Builder builder;
  int folded = 0;

  runRewriter([&](LShiftImmOp *op) {
    auto x = op->getOperand().defining;
    auto imm = V(op);

    if (INT(x)) {
      folded++;
      builder.replace<IntOp>(op, { new IntAttr(V(x) << imm) });
      return true;
    }

    return false;
  });

  runRewriter([&](RShiftImmOp *op) {
    auto x = op->getOperand().defining;
    auto imm = V(op);

    if (INT(x)) {
      folded++;
      builder.replace<IntOp>(op, { new IntAttr(V(x) >> imm) });
      return true;
    }

    return false;
  });

  return folded;
}

void LateConstFold::run() {
  bool changed;
  do {
    changed = false;
    RegularFold fold(module);
    fold.run();

    int folded = fold.stats()["folded-ops"];

    // Run some specific folds at this stage.
    folded += foldImpl();
    foldedTotal += folded;

    if (folded > 0)
      changed = true;
  } while (changed);
}
