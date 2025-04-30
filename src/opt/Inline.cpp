#include "Passes.h"

using namespace sys;

std::map<std::string, int> Inline::stats() {
  return {
    { "inlined-functions", inlined }
  };
}

void Inline::run() {
  
}
