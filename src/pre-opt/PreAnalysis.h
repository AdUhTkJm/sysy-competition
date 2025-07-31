#ifndef PRE_ANALYSIS
#define PRE_ANALYSIS

#include "../opt/Pass.h"
#include "../codegen/Ops.h"
#include "../codegen/Attrs.h"
#include "PreAttrs.h"

namespace sys {

// Marks addresses, loads and stores with `SubscriptAttr`.
class ArrayAccess : public Pass {
  // Takes all induction variables outside the current loop,
  // including that of the loop we're inspecting.
  // (In other words, outer.size() >= 1.)
  void runImpl(Op *loop, std::vector<Op*> outer);
public:
  ArrayAccess(ModuleOp *module): Pass(module) {}
    
  std::string name() override { return "array-access"; };
  std::map<std::string, int> stats() override { return {}; };
  void run() override;
};

// Marks base of an array.
class Base : public Pass {
  void runImpl(Region *region);
public:
  Base(ModuleOp *module): Pass(module) {}
    
  std::string name() override { return "base"; };
  std::map<std::string, int> stats() override { return {}; };
  void run() override;
};

// Checks whether a loop is parallelizable.
class Parallelizable : public Pass {
  void runImpl(Op *loop, int depth);
public:
  Parallelizable(ModuleOp *module): Pass(module) {}
    
  std::string name() override { return "parallelizable"; };
  std::map<std::string, int> stats() override { return {}; };
  void run() override;
};

// Checks whether a function does not store to global variable.
class NoStore : public Pass {
  void runImpl(Op *func);
public:
  NoStore(ModuleOp *module): Pass(module) {}
    
  std::string name() override { return "no-store"; };
  std::map<std::string, int> stats() override { return {}; };
  void run() override;
};

}

#endif
