#include "Passes.h"

using namespace sys;

// Checks whether every operation dominates its uses.
static void checkDom(Region *region) {
  for (auto bb : region->getBlocks()) {
    for (auto op : bb->getOps()) {
      // Phi's are checked later on.
      if (isa<PhiOp>(op))
        continue;

      for (auto operand : op->getOperands()) {
        auto def = operand.defining;
        if (!def->getParent()->dominates(bb)) {
          std::cerr << "non-dominating: " << op;
          std::cerr << "operand: " << def;
          assert(false);
        }
      }
    }
  }
};

void Verify::run() {
  auto funcs = collectFuncs();
  for (auto func : funcs) {
    auto region = func->getRegion();
    region->updateDoms();
    checkDom(region);
  }

  auto phis = module->findAll<PhiOp>();
  for (auto phi : phis) {
    // Check the number of phi's must be equal to the number of predecessors.
    auto parent = phi->getParent();
    assert(parent->getPreds().size() == phi->getOperandCount());

    // Check that all operands from Phi must come from the immediate predecessor.
    auto bb = phi->getParent();
    for (auto attr : phi->getAttrs()) {
      if (!bb->getPreds().count(FROM(attr))) {
        std::cerr << "phi operands are not from predecessor:\n  ";
        phi->dump(std::cerr);
        assert(false);
      }
    }
  }
}
