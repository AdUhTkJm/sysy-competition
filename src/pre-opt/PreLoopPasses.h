#ifndef PRE_LOOP_PASSES_H
#define PRE_LOOP_PASSES_H

#include "../opt/Pass.h"
#include "../codegen/CodeGen.h"
#include "../codegen/Ops.h"
#include "../codegen/Attrs.h"
#include "PreAttrs.h"
#include <unordered_set>

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

// Determine whether a const array is a view of another.
// In that case, inline it.
class View : public Pass {
  int inlined = 0;
  
  std::unordered_map<std::string, std::unordered_set<Op*>> usedIn;
  void runImpl(Op *func);
public:
  View(ModuleOp *module): Pass(module) {}

  std::string name() override { return "view"; }
  std::map<std::string, int> stats() override;
  void run() override;
};

// Erase useless loops.
class LoopDCE : public Pass {
  int erased = 0;
public:
  LoopDCE(ModuleOp *module): Pass(module) {}

  std::string name() override { return "loop-dce"; }
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

// Loop unswitch.
// Unswitch branches related to induction variable.
class Unswitch : public Pass {
  int unswitched = 0;

  bool runImpl(Op *loop);
  bool cmpmod(Op *loop, Op *cond);
  bool ltconst(Op *loop, Op *cond);
  bool gtconst(Op *loop, Op *cond);
public:
  Unswitch(ModuleOp *module): Pass(module) {}

  std::string name() override { return "unswitch"; }
  std::map<std::string, int> stats() override;
  void run() override;
};

class Unroll : public Pass {
  int unrolled = 0;
public:
  Unroll(ModuleOp *module): Pass(module) {}

  std::string name() override { return "unroll"; }
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
