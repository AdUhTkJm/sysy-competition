#ifndef CLEANUP_PASSES_H
#define CLEANUP_PASSES_H

#include "Pass.h"
#include "../codegen/CodeGen.h"
#include "../codegen/Attrs.h"

namespace sys {

// Moves all alloca to the beginning.
class MoveAlloca : public Pass {
public:
  MoveAlloca(ModuleOp *module): Pass(module) {}
    
  std::string name() { return "move-alloca"; };
  std::map<std::string, int> stats() { return {}; };
  void run();
};

// Dead code elimination. Deals with functions, basic blocks and variables.
class DCE : public Pass {
  std::vector<Op*> removeable;
  int elimOp = 0;
  int elimFn = 0;
  int elimBB = 0;
  bool elimBlocks;

  bool isImpure(Op *op);
  bool markImpure(Region *region);
  void runOnRegion(Region *region);

  std::map<std::string, FuncOp*> fnMap;
public:
  // If DCE is called before flatten cfg, then it shouldn't eliminate blocks,
  // since the blocks aren't actually well-formed.
  DCE(ModuleOp *module, bool elimBlocks = true): Pass(module), elimBlocks(elimBlocks) {}
    
  std::string name() { return "dce"; };
  std::map<std::string, int> stats();
  void run();
};

// Dead (actually, redundant) load elimination.
class DLE : public Pass {
  int elim = 0;

  void runImpl(Region *region);
public:
  DLE(ModuleOp *module): Pass(module) {}

  std::string name() { return "dle"; }
  std::map<std::string, int> stats();
  void run();
};

// Dead argument elimination.
class DAE : public Pass {
  int elim = 0;
  int elimRet = 0;

  void runImpl(Region *region);
public:
  DAE(ModuleOp *module): Pass(module) {}

  std::string name() { return "dae"; }
  std::map<std::string, int> stats();
  void run();
};

// Dead store elimination.
class DSE : public Pass {
  std::map<Op*, bool> used;

  int elim = 0;

  void dfs(BasicBlock *current, DomTree &dom, std::set<Op*> live);
  void runImpl(Region *region);
public:
  DSE(ModuleOp *module): Pass(module) {}

  std::string name() { return "dse"; };
  std::map<std::string, int> stats();
  void run();
};

class SimplifyCFG : public Pass {
  int inlined = 0;

  void runImpl(Region *region);
public:
  SimplifyCFG(ModuleOp *module): Pass(module) {}

  std::string name() { return "simplify-cfg"; };
  std::map<std::string, int> stats();
  void run();
};

}

#endif
