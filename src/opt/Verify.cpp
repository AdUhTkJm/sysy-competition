#include "Passes.h"

using namespace sys;

// Checks whether every operation dominates its uses.
static void checkDom(Region *region, Op *module) {
  // Only find reachable blocks.
  std::set<BasicBlock*> reachable;
  std::vector<BasicBlock*> queue { region->getFirstBlock() };
  while (!queue.empty()) {
    auto bb = queue.back();
    queue.pop_back();
    
    if (reachable.count(bb))
      continue;
    reachable.insert(bb);
    for (auto succ : bb->getSuccs())
      queue.push_back(succ);
  }

  for (auto bb : reachable) {
    for (auto op : bb->getOps()) {
      // Phi's are checked later on.
      if (isa<PhiOp>(op))
        continue;

      for (auto operand : op->getOperands()) {
        auto def = operand.defining;
        if (!def->getParent()->dominates(bb)) {
          std::cerr << module << "non-dominating: " << op << "operand: " << def;
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
    checkDom(region, module);
  }

  auto phis = module->findAll<PhiOp>();
  for (auto phi : phis) {
    // Check the number of phi's must be equal to the number of predecessors.
    auto parent = phi->getParent();
    if (parent->getPreds().size() != phi->getOperandCount()) {
      std::cerr << module << "phi without enough operands:\n  " << phi;
      assert(false);
    }

    // Check that all operands from Phi must come from the immediate predecessor.
    auto bb = phi->getParent();
    for (auto attr : phi->getAttrs()) {
      if (!bb->getPreds().count(FROM(attr))) {
        std::cerr << module << "phi operands are not from predecessor:\n  " << phi;
        assert(false);
      }
    }
  }
}
