#ifndef ARM_PASSES_H
#define ARM_PASSES_H

#include "../opt/Pass.h"
#include "../codegen/CodeGen.h"
#include "../codegen/Ops.h"
#include "../codegen/Attrs.h"
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
  int spilled;
  int convertedTotal;

  std::map<FuncOp*, std::set<Reg>> usedRegisters;
  std::map<std::string, FuncOp*> fnMap;

  std::map<Op*, std::set<Op*>> interf;
  std::map<Op*, std::set<Reg>> forbidden;
  std::map<Op*, Reg> assignment;
  std::unordered_map<Op*, int> spillOffset;

  void runImpl(Region *region, bool isLeaf);
  void proEpilogue(FuncOp *funcOp, bool isLeaf);
  int latePeephole(Op *funcOp);
  void tidyup(Region *region);
public:

  RegAlloc(ModuleOp *module): Pass(module) {}

  std::string name() { return "arm-regalloc"; };
  std::map<std::string, int> stats();
  void run();
};

// Dumps the output.
class Dump : public Pass {
  std::string out;

  void dump(std::ostream &os);
  void dumpBody(Region *region, std::ostream &os);
  void dumpOp(Op *op, std::ostream &os);
public:
  Dump(ModuleOp *module, const std::string &out): Pass(module), out(out) {}

  std::string name() { return "arm-dump"; };
  std::map<std::string, int> stats() { return {}; }
  void run();
};

};

#endif
