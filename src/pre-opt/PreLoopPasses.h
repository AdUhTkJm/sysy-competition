#ifndef PRE_LOOP_PASSES_H
#define PRE_LOOP_PASSES_H

#include "../opt/Pass.h"
#include "../codegen/CodeGen.h"
#include "../codegen/Ops.h"
#include "../codegen/Attrs.h"

namespace sys {

// Raise whiles to fors whenever possible.
class RaiseToFor : public Pass {
  int raised = 0;
public:
  RaiseToFor(ModuleOp *module): Pass(module) {}

  std::string name() override { return "raise-to-for"; }
  std::map<std::string, int> stats() override;
  void run() override;
};

// Loop fusion.
class Fusion : public Pass {
  int fused = 0;

  void runImpl(FuncOp *func);
public:
  Fusion(ModuleOp *module): Pass(module) {}

  std::string name() override { return "fusion"; }
  std::map<std::string, int> stats() override;
  void run() override;
};

// Lower operations back to its original form.
class Lower : public Pass {
public:
  Lower(ModuleOp *module): Pass(module) {}
  
  std::string name() override { return "lower"; }
  std::map<std::string, int> stats() override { return {}; }
  void run() override;
};

}

#endif
