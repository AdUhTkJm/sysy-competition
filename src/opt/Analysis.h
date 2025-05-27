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

public:
  Pureness(ModuleOp *module): Pass(module) {}
    
  std::string name() override { return "pureness"; };
  std::map<std::string, int> stats() override { return {}; }
  void run() override;
};

// Puts CallerAttr to each function.
class CallGraph : public Pass {
public:
  CallGraph(ModuleOp *module): Pass(module) {}
    
  std::string name() override { return "call-graph"; };
  std::map<std::string, int> stats() override { return {}; }
  void run() override;
};

// Gives an AliasAttr to values, if they are addresses.
class Alias : public Pass {
  std::map<std::string, GlobalOp*> gMap;
  void runImpl(Region *region);
public:
  Alias(ModuleOp *module): Pass(module) {}

  std::string name() override { return "alias"; };
  std::map<std::string, int> stats() override { return {}; }
  void run() override;
};

// Integer range analysis.
class Range : public Pass {
  // The set of all loop headers in a function.
  // We should apply widening at these blocks, otherwise it would take forever to converge.
  std::set<BasicBlock*> headers;

  void runImpl(Region *region, const LoopForest &forest);
public:
  Range(ModuleOp *module): Pass(module) {}

  std::string name() override { return "range"; }
  std::map<std::string, int> stats() override { return {}; }
  void run() override;
};

// Positivity analysis. Very coarse, but suffices for now.
class Positive : public Pass {
public:
  Positive(ModuleOp *module): Pass(module) {}
    
  std::string name() override { return "positive"; };
  std::map<std::string, int> stats() override { return {}; }
  void run() override;
};

// Mark functions that are called at most once.
class AtMostOnce : public Pass {
public:
  AtMostOnce(ModuleOp *module): Pass(module) {}
    
  std::string name() override { return "at-most-once"; };
  std::map<std::string, int> stats() override { return {}; }
  void run() override;
};

}

#endif
