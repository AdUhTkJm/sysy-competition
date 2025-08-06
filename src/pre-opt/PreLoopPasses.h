#ifndef PRE_LOOP_PASSES_H
#define PRE_LOOP_PASSES_H

#include "../opt/Pass.h"
#include "../codegen/CodeGen.h"
#include "../codegen/Ops.h"
#include "../codegen/Attrs.h"
#include "PreAttrs.h"
#include <unordered_set>

namespace sys {

// Raise whiles to fors whenever possible.
class RaiseToFor : public Pass {
  int raised = 0;
public:
  RaiseToFor(ModuleOp *module): Pass(module) {}

  std::string name() override { return "raise-to-for"; }
  std::map<std::string, int> stats() override;
  void run() override;
};

// Determine whether a const array is a view of another.
// In that case, inline it.
class View : public Pass {
  int inlined = 0;
  
  std::unordered_map<std::string, std::unordered_set<Op*>> usedIn;
  void runImpl(Op *func);
public:
  View(ModuleOp *module): Pass(module) {}

  std::string name() override { return "view"; }
  std::map<std::string, int> stats() override;
  void run() override;
};

// Erase useless loops.
class LoopDCE : public Pass {
  int erased = 0;
public:
  LoopDCE(ModuleOp *module): Pass(module) {}

  std::string name() override { return "loop-dce"; }
  std::map<std::string, int> stats() override;
  void run() override;
};

// Loop fusion.
class Fusion : public Pass {
  int fused = 0;

  void runImpl(FuncOp *func);
public:
  Fusion(ModuleOp *module): Pass(module) {}

  std::string name() override { return "fusion"; }
  std::map<std::string, int> stats() override;
  void run() override;
};

// Loop unswitch.
// Unswitch branches related to induction variable.
class Unswitch : public Pass {
  int unswitched = 0;

  bool runImpl(Op *loop);
  bool cmpmod(Op *loop, Op *cond);
  bool ltconst(Op *loop, Op *cond);
  bool gtconst(Op *loop, Op *cond);
  bool invariant(Op *loop, Op *cond);
public:
  Unswitch(ModuleOp *module): Pass(module) {}

  std::string name() override { return "unswitch"; }
  std::map<std::string, int> stats() override;
  void run() override;
};

// For 2D arrays, if their access pattern suggests so,
// then reorder them into column major format.
class ColumnMajor : public Pass {
  struct AccessData {
    int depth = 0;
    // `true` when there exists a loop such that this array's last dimension is
    // contiguously accessed on the deepest dimension;
    // i.e. it's the smallest entry in SubscriptAttr.
    bool contiguous = false;
    // `true` when there exists a loop such that this array's last dimension is
    // NOT contiguously accessed on the deepest dimension.
    bool jumping = false;
    // `false` when some accesses to the array do not have SubscriptAttr.
    bool valid = true;
  };
  std::unordered_map<Op*, AccessData> data;

  void collectDepth(Region *region, int depth);
public:
  ColumnMajor(ModuleOp *module): Pass(module) {}

  std::string name() override { return "column-major"; }
  std::map<std::string, int> stats() override { return {}; }
  void run() override;
};

class Parallelize : public Pass {
public:
  Parallelize(ModuleOp *module): Pass(module) {}

  std::string name() override { return "parallelize"; }
  std::map<std::string, int> stats() override { return {}; }
  void run() override;
};

class Unroll : public Pass {
  int unrolled = 0;
public:
  Unroll(ModuleOp *module): Pass(module) {}

  std::string name() override { return "unroll"; }
  std::map<std::string, int> stats() override;
  void run() override;
};

// Try to hoist out access to adjacent elements of an array out of a loop.
// This is done by GVN in LLVM, but I have no idea how it works there.
class Adjacency : public Pass {
  void runImpl(Op *loop);
public:
  Adjacency(ModuleOp *module): Pass(module) {}
  
  std::string name() override { return "adjacency"; }
  std::map<std::string, int> stats() override { return {}; }
  void run() override;
};

// Lower operations back to its original form.
class Lower : public Pass {
public:
  Lower(ModuleOp *module): Pass(module) {}
  
  std::string name() override { return "lower"; }
  std::map<std::string, int> stats() override { return {}; }
  void run() override;
};

}

#endif
