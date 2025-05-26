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
  
  std::string name() override { return "arm-lower"; };
  std::map<std::string, int> stats() override { return {}; };
  void run() override;
};

class InstCombine : public Pass {
  int combined = 0;
public:
  InstCombine(ModuleOp *module): Pass(module) {}

  std::string name() override { return "arm-inst-combine"; };
  std::map<std::string, int> stats() override;
  void run() override;
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
    
  std::string name() override { return "arm-dce"; };
  std::map<std::string, int> stats() override;
  void run() override;
};

class RegAlloc : public Pass {
  int spilled = 0;
  int convertedTotal = 0;

  std::map<FuncOp*, std::set<Reg>> usedRegisters;
  std::map<std::string, FuncOp*> fnMap;

  void runImpl(Region *region, bool isLeaf);
  void proEpilogue(FuncOp *funcOp, bool isLeaf);
  int latePeephole(Op *funcOp);
  void tidyup(Region *region);
public:

  RegAlloc(ModuleOp *module): Pass(module) {}

  std::string name() override { return "arm-regalloc"; };
  std::map<std::string, int> stats() override;
  void run() override;
};

class LateLegalize : public Pass {
public:
  LateLegalize(ModuleOp *module): Pass(module) {}

  std::string name() override { return "arm-late-legalize"; };
  std::map<std::string, int> stats() override { return {}; }
  void run() override;
};

// Dumps the output.
class Dump : public Pass {
  std::string out;

  void dump(std::ostream &os);
  void dumpBody(Region *region, std::ostream &os);
  void dumpOp(Op *op, std::ostream &os);
public:
  Dump(ModuleOp *module, const std::string &out): Pass(module), out(out) {}

  std::string name() override { return "arm-dump"; };
  std::map<std::string, int> stats() override { return {}; }
  void run() override;
};

};

#endif
