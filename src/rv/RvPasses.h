#ifndef RV_PASSES_H
#define RV_PASSES_H

#include "../opt/Pass.h"

namespace sys {

namespace rv {

struct Lower : public Pass {
public:
  Lower(ModuleOp *module): Pass(module) {}
  
  std::string name() { return "rv-lower"; };
  std::map<std::string, int> stats() { return {}; };
  void run();
};

}

}

#endif
