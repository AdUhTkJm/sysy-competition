#include "Passes.h"

using namespace sys;

void ImplicitReturn::run() {
  auto funcs = module->findAll<FuncOp>();
  for (auto func : funcs) {
    auto region = func->getRegion();
    auto bb = region->getLastBlock();
    if (!bb->getOps().size() || !isa<ReturnOp>(bb->getLastOp())) {
      Builder builder;
      builder.setToBlockEnd(bb);
      builder.create<ReturnOp>();
    }
  }
}
