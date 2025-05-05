#ifndef RV_PASSES_H
#define RV_PASSES_H

#include "../opt/Pass.h"
#include "RvAttrs.h"

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

// The only difference with opt/DCE is that `isImpure` behaves differently.
class RvDCE : public Pass {
  std::vector<Op*> removeable;
  int elim = 0;

  bool isImpure(Op *op);
  void markImpure(Region *region);
  void runOnRegion(Region *region);
public:
  RvDCE(ModuleOp *module): Pass(module) {}
    
  std::string name() { return "rv-dce"; };
  std::map<std::string, int> stats();
  void run();
};

// A weak scheduler that only works on basic blocks.
class InstSchedule : public Pass {
  void runImpl(BasicBlock *bb);
public:
  InstSchedule(ModuleOp *module): Pass(module) {}

  std::string name() { return "rv-inst-schedule"; };
  std::map<std::string, int> stats() { return {}; }
  void run();
};

class RegAlloc : public Pass {
  int spilled = 0;
  int convertedTotal = 0;

  std::map<FuncOp*, std::set<Reg>> usedRegisters;
  std::map<std::string, FuncOp*> fnMap;

  void runImpl(Region *region, bool isLeaf);
  // Create both prologue and epilogue of a function.
  void proEpilogue(FuncOp *funcOp, bool isLeaf);
  int latePeephole(Op *funcOp);
  void tidyup(Region *region);
public:
  RegAlloc(ModuleOp *module): Pass(module) {}

  std::string name() { return "rv-regalloc"; };
  std::map<std::string, int> stats();
  void run();
};

// Dumps the output.
class Dump : public Pass {
  std::string out;

  void dump(std::ostream &os);
public:
  Dump(ModuleOp *module, const std::string &out): Pass(module), out(out) {}

  std::string name() { return "rv-dump"; };
  std::map<std::string, int> stats() { return {}; }
  void run();
};

}

}

#endif
