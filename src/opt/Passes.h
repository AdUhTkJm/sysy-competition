#ifndef PASSES_H
#define PASSES_H

#include "Pass.h"

namespace sys {

class FlattenCFG : public Pass {
public:
  FlattenCFG(ModuleOp *module): Pass(module) {}
  
  std::string name() { return "flatten-cfg"; };
  std::map<std::string, int> stats() { return {}; };
  void run();
};

}

#endif
