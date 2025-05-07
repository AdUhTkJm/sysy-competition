#ifndef PASSES_H
#define PASSES_H

#include "Pass.h"
#include "../codegen/CodeGen.h"
#include "../codegen/Attrs.h"

#include <set>

namespace sys {

class FlattenCFG : public Pass {
public:
  FlattenCFG(ModuleOp *module): Pass(module) {}
  
  std::string name() { return "flatten-cfg"; };
  std::map<std::string, int> stats() { return {}; };
  void run();
};

// Moves all alloca to the beginning.
class MoveAlloca : public Pass {
public:
  MoveAlloca(ModuleOp *module): Pass(module) {}
    
  std::string name() { return "move-alloca"; };
  std::map<std::string, int> stats() { return {}; };
  void run();
};

// Adds an implicit return if that's not present.
class ImplicitReturn : public Pass {
public:
  ImplicitReturn(ModuleOp *module): Pass(module) {}
    
  std::string name() { return "implicit-return"; };
  std::map<std::string, int> stats() { return {}; };
  void run();
};

// Converts alloca's to SSA values.
// This must run on flattened CFG, otherwise `break` and `continue` are hard to deal with.
class Mem2Reg : public Pass {
  int count = 0;  // Total converted count
  int missed = 0; // Unconvertible alloca's

  void runImpl(FuncOp *func);
  void fillPhi(BasicBlock *bb, BasicBlock *last);

  // Maps AllocaOp* to Value (the real value of this alloca).
  using SymbolTable = std::map<Op*, Value>;
  SymbolTable symbols;
  std::map<PhiOp*, AllocaOp*> phiFrom;
  std::set<BasicBlock*> visited;
  // Allocas we're going to convert in the pass.
  std::set<Op*> converted;

  class SemanticScope {
    Mem2Reg &pass;
    SymbolTable symbols;
  public:
    SemanticScope(Mem2Reg &pass): pass(pass), symbols(pass.symbols) {}
    ~SemanticScope() { pass.symbols = symbols; }
  };
public:
  Mem2Reg(ModuleOp *module): Pass(module) {}
    
  std::string name() { return "mem2reg"; };
  std::map<std::string, int> stats();
  void run();
};

// Analysis pass.
// Detects whether a function is pure. If it isn't, give it an ImpureAttr.
class Pureness : public Pass {
  // Maps a function to all functions that it might call.
  std::map<FuncOp*, std::set<FuncOp*>> callGraph;

  void predetermineImpure(FuncOp *func);
public:
  Pureness(ModuleOp *module): Pass(module) {}
    
  std::string name() { return "pureness-analysis"; };
  std::map<std::string, int> stats() { return {}; }
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

class StrengthReduct : public Pass {
  int convertedTotal = 0;

  int runImpl();
public:
  StrengthReduct(ModuleOp *module): Pass(module) {}
    
  std::string name() { return "strength-reduction"; };
  std::map<std::string, int> stats();
  void run();
};

// Global value numbering.
class GVN : public Pass {
  int elim = 0;

  using SymbolTable = std::unordered_map<Op*, int>;
  using Domtree = std::map<BasicBlock*, std::vector<BasicBlock*>>;

  // The number of each Op.
  SymbolTable symbols;

  struct Expr {
    int id;
    std::vector<int> operands;

    // Attributes
    int vi = 0;
    float vf = 0;
    std::string name;

    bool operator<(const Expr &other) const;
  };
  std::map<Expr, int> exprNum;
  std::map<int, Op*> numOp;
  // The current number.
  int num = 1;

  class SemanticScope {
    GVN &pass;
    SymbolTable symbols;
    std::map<Expr, int> exprNum;
    std::map<int, Op*> numOp;
  public:
    SemanticScope(GVN &pass):
      pass(pass), symbols(pass.symbols), exprNum(pass.exprNum), numOp(pass.numOp) {}
    ~SemanticScope() {
      pass.symbols = symbols;
      pass.exprNum = exprNum;
      pass.numOp = numOp;
    }
  };

  // Dominator-based Value Numbering Technique. See Briggs.
  void dvnt(BasicBlock *bb, Domtree &domtree);
public:
  GVN(ModuleOp *module): Pass(module) {}
    
  std::string name() { return "gvn"; };
  std::map<std::string, int> stats();
  void run();
  void runImpl(Region *region);
};

// Puts CallerAttr to each function.
class CallGraph : public Pass {
public:
  CallGraph(ModuleOp *module): Pass(module) {}
    
  std::string name() { return "call-graph"; };
  std::map<std::string, int> stats() { return {}; }
  void run();
};

class Inline : public Pass {
  int inlined = 0;

  // Do not inline functions with Op count > `threshold`.
  int threshold;
  std::map<std::string, FuncOp*> fnMap;
public:
  Inline(ModuleOp *module, int threshold): Pass(module), threshold(threshold) {}
    
  std::string name() { return "inline"; };
  std::map<std::string, int> stats();
  void run();
};

// Folds before flattening CFG.
class EarlyConstFold : public Pass {
  int foldedTotal = 0;

  int foldImpl();
public:
  EarlyConstFold(ModuleOp *module): Pass(module) {}
    
  std::string name() { return "early-const-fold"; };
  std::map<std::string, int> stats();
  void run();
};

// Rewrites WhileOp into ForOp, if possible.
class RecognizeFor : public Pass {
  void runImpl(Region *region);
public:
  RecognizeFor(ModuleOp *module): Pass(module) {}
    
  std::string name() { return "for-recognition"; };
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

// Localizes global variables.
class Localize : public Pass {
  bool beforeFlatten;
public:
  Localize(ModuleOp *module, bool beforeFlatten):
    Pass(module), beforeFlatten(beforeFlatten) {}
    
  std::string name() { return "localize"; };
  std::map<std::string, int> stats() { return {}; }
  void run();
};

// Globalizes local arrays.
class Globalize : public Pass {
  void runImpl(Region *region);
public:
  Globalize(ModuleOp *module): Pass(module) {}
    
  std::string name() { return "globalize"; };
  std::map<std::string, int> stats() { return {}; }
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

// Global code motion.
class LoopInfo;
class LoopForest;
class GCM : public Pass {
  std::set<Op*> visited;
  DomTree tree;
  // This is the depth on dominator tree.
  std::map<BasicBlock*, int> depth;
  // This is the depth in loop forest.
  std::map<BasicBlock*, int> loopDepth;
  std::map<Op*, BasicBlock*> scheduled;

  void updateDepth(BasicBlock *bb, int dep);
  void updateLoopDepth(LoopInfo *info, int dep);

  void scheduleEarly(BasicBlock *entry, Op *op);
  void scheduleLate(Op *op);

  // Lowest common ancestor.
  BasicBlock *lca(BasicBlock *a, BasicBlock *b);

  void runImpl(Region *region, const LoopForest &forest);
public:
  GCM(ModuleOp *module): Pass(module) {}

  std::string name() { return "gcm"; };
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

// Folds a wide range of expressions.
class RegularFold : public Pass {
  int foldedTotal = 0;

  int foldImpl();
public:
  RegularFold(ModuleOp *module): Pass(module) {}
    
  std::string name() { return "regular-fold"; };
  std::map<std::string, int> stats();
  void run();
};

// Folds after mem2reg.
class LateConstFold : public Pass {
  int foldedTotal = 0;

  int foldImpl();
public:
  LateConstFold(ModuleOp *module): Pass(module) {}
    
  std::string name() { return "late-const-fold"; };
  std::map<std::string, int> stats();
  void run();
};

}

#endif
