#include "Passes.h"

using namespace sys;

void Range::runImpl(Region *region) {
  
}

void Range::run() {
  auto funcs = collectFuncs();

  for (auto func : funcs)
    runImpl(func->getRegion());
}
