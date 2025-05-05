#include "RvPasses.h"

using namespace sys;
using namespace sys::rv;

void InstSchedule::runImpl(BasicBlock *bb) {
  // First, we need to build a dependence graph between loads/stores.
  
}

void InstSchedule::run() {
  auto funcs = collectFuncs();
  for (auto func : funcs) {
    auto region = func->getRegion();
    for (auto bb : region->getBlocks())
      runImpl(bb);
  }
}
