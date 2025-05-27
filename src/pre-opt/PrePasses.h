#ifndef PREPASSES_H
#define PREPASSES_H

#include "../opt/Pass.h"
#include "../codegen/CodeGen.h"
#include "../codegen/Attrs.h"

namespace sys {

// Folds before flattening CFG.
class EarlyConstFold : public Pass {
  int foldedTotal = 0;
  bool beforePureness;

  int foldImpl();
public:
  EarlyConstFold(ModuleOp *module, bool beforePureness): Pass(module), beforePureness(beforePureness) {}
    
  std::string name() { return "early-const-fold"; };
  std::map<std::string, int> stats();
  void run();
};

// Localizes global variables.
class Localize : public Pass {
  bool beforeFlatten;
public:
  Localize(ModuleOp *module, bool beforeFlatten):
    Pass(module), beforeFlatten(beforeFlatten) {}
    
  std::string name() { return "localize"; };
  std::map<std::string, int> stats() { return {}; }
  void run();
};

// Tail call optimization.
class TCO : public Pass {
  int uncalled = 0;

  void runImpl(FuncOp *func);
public:
  TCO(ModuleOp *module): Pass(module) {}
    
  std::string name() { return "tco"; };
  std::map<std::string, int> stats();
  void run();
};

}

#endif
