#include "Passes.h"

using namespace sys;

void Verify::run() {
  auto funcs = collectFuncs();
  for (auto func : funcs)
    func->getRegion()->updatePreds();
  
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
    
    // Check phi doesn't reference itself.
    for (auto operand : phi->getOperands())
      assert(operand.defining != phi);
  }
}
