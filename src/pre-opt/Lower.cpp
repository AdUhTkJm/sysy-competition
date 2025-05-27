#include "PreLoopPasses.h"

using namespace sys;

void Lower::run() {
  auto loops = module->findAll<ForOp>();

  Builder builder;
  // Destruct fors and turn them into whiles.
  for (auto loop : loops) {
    builder.setBeforeOp(loop);
    auto alloca = builder.create<AllocaOp>({ new SizeAttr(4) });
  }
}
