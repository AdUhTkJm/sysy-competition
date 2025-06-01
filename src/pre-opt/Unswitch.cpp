#include "PreLoopPasses.h"

using namespace sys;

std::map<std::string, int> Unswitch::stats() {
  return {
    { "unswitched-loops", unswitched },
  };
}

void Unswitch::run() {
  auto loops = module->findAll<ForOp>();

  for (auto loop : loops) {

  }
}
