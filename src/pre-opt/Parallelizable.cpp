#include "PreAnalysis.h"

using namespace sys;

void Parallelizable::runImpl(Op *loop) {
  // Record the affine subscript for each relevant op.
  std::unordered_map<Op*, int> index;

  // Analyze dependency.
  // Find all loads and stores in the loop.
  
}

void Parallelizable::run() {
  ArrayAccess(module).run();
  Base(module).run();

  auto loops = module->findAll<ForOp>();

  for (auto loop : loops)
    runImpl(loop);
}
