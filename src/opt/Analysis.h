#ifndef ANALYSIS_H
#define ANALYSIS_H

#include "Passes.h"
#include "../codegen/CodeGen.h"
#include "../codegen/Attrs.h"

namespace sys {

// Analysis pass.
// Detects whether a function is pure. If it isn't, give it an ImpureAttr.
class Pureness : public Pass {
  // Maps a function to all functions that it might call.
  std::map<FuncOp*, std::set<FuncOp*>> callGraph;

  void predetermineImpure(FuncOp *func);
public:
  Pureness(ModuleOp *module): Pass(module) {}
    
  std::string name() { return "pureness"; };
  std::map<std::string, int> stats() { return {}; }
  void run();
};

// Puts CallerAttr to each function.
class CallGraph : public Pass {
public:
  CallGraph(ModuleOp *module): Pass(module) {}
    
  std::string name() { return "call-graph"; };
  std::map<std::string, int> stats() { return {}; }
  void run();
};

// Gives an AliasAttr to values, if they are addresses.
class Alias : public Pass {
  std::map<std::string, GlobalOp*> gMap;
  void runImpl(Region *region);
public:
  Alias(ModuleOp *module): Pass(module) {}

  std::string name() { return "alias"; };
  std::map<std::string, int> stats() { return {}; }
  void run();
};

// Integer range analysis.
class Range : public Pass {
  // The set of all loop headers in a function.
  // We should apply widening at these blocks, otherwise it would take forever to converge.
  std::set<BasicBlock*> headers;

  void runImpl(Region *region, const LoopForest &forest);
public:
  Range(ModuleOp *module): Pass(module) {}

  std::string name() { return "range"; }
  std::map<std::string, int> stats() { return {}; }
  void run();
};

// Mark functions that are called at most once.
class AtMostOnce : public Pass {
public:
  AtMostOnce(ModuleOp *module): Pass(module) {}
    
  std::string name() { return "at-most-once"; };
  std::map<std::string, int> stats() { return {}; }
  void run();
};

}

#endif
