#include "Passes.h"

using namespace sys;

void Verify::run() {
  auto phis = module->findAll<PhiOp>();
  for (auto phi : phis) {
    // Check that all operands from Phi must come from the immediate predecessor.
    auto bb = phi->getParent();
    for (auto attr : phi->getAttrs())
      assert(bb->getPreds().count(FROM(attr)));
    for (auto operand : phi->getOperands())
      assert(operand.defining != phi);
  }
}
