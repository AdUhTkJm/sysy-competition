#ifndef ARM_PASSES_H
#define ARM_PASSES_H

#include "../opt/Pass.h"
#include "../codegen/Ops.h"
#include "../codegen/CodeGen.h"
#include "ArmOps.h"
#include "ArmAttrs.h"

namespace sys::arm {

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

class RegAlloc : public Pass {
  std::map<FuncOp*, std::set<Reg>> usedRegisters;
  std::map<std::string, FuncOp*> fnMap;

  std::map<Op*, std::set<Op*>> interf;
  std::map<Op*, std::set<Reg>> forbidden;

  // Fill in the internal data structures.
  void allocate(Region *region, bool isLeaf);
public:
  std::map<Op*, Reg> assignment;
  std::unordered_map<Op*, int> spillOffset;

  RegAlloc(ModuleOp *module): Pass(module) {}

  std::string name() { return "arm-regalloc"; };
  std::map<std::string, int> stats() { return {}; }
  void run();
};

class Destruct : public Pass {
  std::map<Op*, Reg> assignment;
  std::unordered_map<Op*, int> spillOffset;

  void runImpl(Region *region);
  bool spilled(Op *op);
public:
  Destruct(ModuleOp *module): Pass(module) {}

  std::string name() { return "arm-destruct"; };
  std::map<std::string, int> stats() { return {}; }
  void run();
};

// Dumps the output.
class Dump : public Pass {
  std::string out;

  void dump(std::ostream &os);
  void dumpBody(Region *region, std::ostream &os);
public:
  Dump(ModuleOp *module, const std::string &out): Pass(module), out(out) {}

  std::string name() { return "arm-dump"; };
  std::map<std::string, int> stats() { return {}; }
  void run();
};

};

#endif
