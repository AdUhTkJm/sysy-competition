#include "Passes.h"

using namespace sys;

void Alias::runImpl(Region *region) {
  
}

void Alias::run() {
  auto funcs = collectFuncs();

  for (auto func : funcs)
    runImpl(func->getRegion());
}
