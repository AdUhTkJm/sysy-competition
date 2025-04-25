#ifndef PASSES_H
#define PASSES_H

#include "Pass.h"
#include <set>

namespace sys {

class FlattenCFG : public Pass {
public:
  FlattenCFG(ModuleOp *module): Pass(module) {}
  
  std::string name() { return "flatten-cfg"; };
  std::map<std::string, int> stats() { return {}; };
  void run();
};

// Moves all alloca to the beginning.
class MoveAlloca : public Pass {
public:
  MoveAlloca(ModuleOp *module): Pass(module) {}
    
  std::string name() { return "move-alloca"; };
  std::map<std::string, int> stats() { return {}; };
  void run();
};

// Converts alloca's to SSA values.
// This must run on flattened CFG, otherwise `break` and `continue` are hard to deal with.
class Mem2Reg : public Pass {
  int count = 0;  // Total converted count
  int missed = 0; // Unconvertible alloca's

  void runImpl(FuncOp *func);
  void fillPhi(BasicBlock *bb);

  // Maps AllocaOp* to Value (the real value of this alloca).
  using SymbolTable = std::map<Op*, Value>;
  SymbolTable symbols;
  std::map<PhiOp*, AllocaOp*> phiFrom;
  std::set<BasicBlock*> visited;
  // Allocas we're going to convert in the pass.
  std::set<Op*> converted;

  class SemanticScope {
    Mem2Reg &pass;
    SymbolTable symbols;
  public:
    SemanticScope(Mem2Reg &pass): pass(pass), symbols(pass.symbols) {}
    ~SemanticScope() { pass.symbols = symbols; }
  };
public:
  Mem2Reg(ModuleOp *module): Pass(module) {}
    
  std::string name() { return "mem2reg"; };
  std::map<std::string, int> stats();
  void run();
};

// Analysis pass.
// Detects whether a function is pure. If it isn't, give it an ImpureAttr.
class Pureness : public Pass {
  // Maps a function to all functions that it might call.
  std::map<FuncOp*, std::set<FuncOp*>> callGraph;

  void predetermineImpure(FuncOp *func);
public:
  Pureness(ModuleOp *module): Pass(module) {}
    
  std::string name() { return "pureness-analysis"; };
  std::map<std::string, int> stats() { return {}; }
  void run();
};

// Dead variable elimination.
class DCE : public Pass {
  std::vector<Op*> removeable;
  int elim = 0;

  bool isImpure(Op *op);
  bool markImpure(Region *region);
  void runOnRegion(Region *region);
public:
  DCE(ModuleOp *module): Pass(module) {}
    
  std::string name() { return "dce"; };
  std::map<std::string, int> stats();
  void run();
};

}

#endif
