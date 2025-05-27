#ifndef PREPASSES_H
#define PREPASSES_H

#include "../opt/Pass.h"
#include "../codegen/CodeGen.h"
#include "../codegen/Attrs.h"

namespace sys {

// Moves all alloca to the beginning.
class MoveAlloca : public Pass {
public:
  MoveAlloca(ModuleOp *module): Pass(module) {}
    
  std::string name() override { return "move-alloca"; };
  std::map<std::string, int> stats() override { return {}; };
  void run() override;
};

// Folds before flattening CFG.
class EarlyConstFold : public Pass {
  int foldedTotal = 0;
  bool beforePureness;

  int foldImpl();
public:
  EarlyConstFold(ModuleOp *module, bool beforePureness): Pass(module), beforePureness(beforePureness) {}
    
  std::string name() override { return "early-const-fold"; };
  std::map<std::string, int> stats() override;
  void run() override;
};

// Localizes global variables.
class Localize : public Pass {
  bool beforeFlatten;
public:
  Localize(ModuleOp *module, bool beforeFlatten):
    Pass(module), beforeFlatten(beforeFlatten) {}
    
  std::string name() override { return "localize"; };
  std::map<std::string, int> stats() override { return {}; }
  void run() override;
};

// Tail call optimization.
class TCO : public Pass {
  int uncalled = 0;

  void runImpl(FuncOp *func);
public:
  TCO(ModuleOp *module): Pass(module) {}
    
  std::string name() override { return "tco"; };
  std::map<std::string, int> stats() override;
  void run() override;
};

// Remerge basic blocks.
// It is always possible to ensure each region has one block (except allocas),
// since before FlattenCFG there's no jumps.
class Remerge : public Pass {
  void runImpl(Region *region);
public:
  Remerge(ModuleOp *module): Pass(module) {}
    
  std::string name() override { return "remerge"; };
  std::map<std::string, int> stats() override { return {}; }
  void run() override;
};

}

#endif
