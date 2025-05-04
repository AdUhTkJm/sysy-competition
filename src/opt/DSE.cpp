#include "Passes.h"

using namespace sys;

std::map<std::string, int> DSE::stats() {
  return {
    { "removed-stores", elim },
  };
}

void DSE::updateLiveness(Region *region) {
  
}

void DSE::run() {
  auto funcs = collectFuncs();

  for (auto func : funcs)
    runImpl(func->getRegion());
}
