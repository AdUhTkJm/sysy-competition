#include "RvAttrs.h"
#include "RvPasses.h"

using namespace sys::rv;
using namespace sys;

std::map<Value, Reg> sys::rv::regalloc;

std::map<std::string, int> RegAlloc::stats() {
  return {
    { "spilled", spilled }
  };
}

void RegAlloc::run() {
  // TODO
}
