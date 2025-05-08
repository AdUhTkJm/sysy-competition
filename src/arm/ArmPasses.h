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

class InstCombine : public Pass {
  int combined = 0;
public:
  InstCombine(ModuleOp *module): Pass(module) {}

  std::string name() { return "arm-inst-combine"; };
  std::map<std::string, int> stats();
  void run();
};

// The only difference with opt/DCE is that `isImpure` behaves differently.
class ArmDCE : public Pass {
  std::vector<Op*> removeable;
  int elim = 0;

  bool isImpure(Op *op);
  void markImpure(Region *region);
  void runOnRegion(Region *region);
public:
  ArmDCE(ModuleOp *module): Pass(module) {}
    
  std::string name() { return "arm-dce"; };
  std::map<std::string, int> stats();
  void run();
};

};

}

#endif
