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

class RegAlloc : public Pass {
  int spilled = 0;

  void runImpl(Region *region, bool isLeaf);
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
