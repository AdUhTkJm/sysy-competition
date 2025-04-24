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

struct RegAlloc : public Pass {
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
