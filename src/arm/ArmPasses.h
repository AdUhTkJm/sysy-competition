#ifndef ARM_PASSES_H
#define ARM_PASSES_H

#include "../opt/Pass.h"
#include "../codegen/Ops.h"
#include "../codegen/Attrs.h"
#include "ArmOps.h"

namespace sys {

namespace arm {

class Lower : public Pass {
public:
  Lower(ModuleOp *module): Pass(module) {}
  
  std::string name() { return "arm-lower"; };
  std::map<std::string, int> stats() { return {}; };
  void run();
};

};

}

#endif
