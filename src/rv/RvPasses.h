#ifndef RV_PASSES_H
#define RV_PASSES_H

#include "../opt/Pass.h"

namespace sys {

namespace rv {

class Lower : public Pass {
public:
  Lower(ModuleOp *module): Pass(module) {}
  
  std::string name() { return "rv-lower"; };
  std::map<std::string, int> stats() { return {}; };
  void run();
};

class InstCombine : public Pass {
  int combined = 0;
public:
  InstCombine(ModuleOp *module): Pass(module) {}

  std::string name() { return "rv-inst-combine"; };
  std::map<std::string, int> stats();
  void run();
};

class RegAlloc : public Pass {
  int spilled = 0;
public:
  RegAlloc(ModuleOp *module): Pass(module) {}

  std::string name() { return "rv-regalloc"; };
  std::map<std::string, int> stats();
  void run();
};

enum class Reg;
// Result of register allocation is stored here.
extern std::map<Value, Reg> regalloc;

}

}

#endif
