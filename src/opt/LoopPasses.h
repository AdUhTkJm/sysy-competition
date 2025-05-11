#ifndef LOOP_PASSES_H
#define LOOP_PASSES_H

#include "Pass.h"
#include "../codegen/CodeGen.h"
#include "../codegen/Attrs.h"

#include <set>
#include <vector>

// The whole content of this file should be run after Mem2Reg.
namespace sys {

// We will take the LLVM terminology:
//   _Header_ is the only block of loop entry;
//   _Preheader_ is the only block in header.preds;
//   _Latch_ is a block with backedge;
//   _Exiting Block_ is a block that jumps out of the loop. 
//   _Exit Block_ is a block that isn't in the loop but is a target of an exitING block.
//
// Note the difference of the last two terms.
class LoopInfo {
  std::vector<LoopInfo*> subloops;
  std::set<BasicBlock*> exitings;
  std::set<BasicBlock*> exits;
  std::set<BasicBlock*> latches;
  std::set<BasicBlock*> bbs;
  BasicBlock *preheader;
  BasicBlock *header;
  LoopInfo *parent = nullptr;
  // Induction variable. Though there might be multiple, we only preserve the first encountered.
  Op *induction = nullptr;
  Op *start = nullptr, *stop = nullptr;
  int step;

  friend class LoopAnalysis;
  friend class LoopForest;
public:
  const auto &getLatches() const { return latches; }
  const auto &getExits() const { return exits; }
  const auto &getExitingBlocks() const { return exitings; }
  const auto &getSubloops() const { return subloops; }
  const auto &getBlocks() const { return bbs; }
  auto getPreheader() const { return preheader; }
  auto getHeader() const { return header; }
  auto getParent() const { return parent; }
  auto getLatch() const { assert(latches.size() == 1); return *latches.begin(); }
  auto getInduction() const { return induction; }
  auto getStart() const { return start; }
  auto getStop() const { return stop; }
  int getStep() const { return step; }

  bool contains(BasicBlock *bb) const { return bbs.count(bb); }

  // Note that this relies on a previous dump of the parent op,
  // otherwise the numbers of blocks are meaningless.
  void dump(std::ostream &os);
};

// Each loop structure is a tree.
// Multiple loops become a forest.
class LoopForest {
  std::vector<LoopInfo*> loops;
  std::map<BasicBlock*, LoopInfo*> loopMap;

  friend class LoopAnalysis;
public:
  const auto &getLoops() const { return loops; }

  LoopInfo *getInfo(BasicBlock *header) { return loopMap[header]; }

  void dump(std::ostream &os);
};

class LoopAnalysis : public Pass {
  std::map<FuncOp*, LoopForest> info;

public:
  LoopAnalysis(ModuleOp *module): Pass(module) {}

  std::string name() { return "loop-analysis"; }
  std::map<std::string, int> stats() { return {}; }
  LoopForest runImpl(Region *region);
  void run();
  void reset() { info = {}; }

  auto getResult() { return info; }
};

// Canonicalize loops. Ensures:
//   1) A single preheader;
//   2) A single latch; (do I really need this?)
//   3) In LCSSA.
class CanonicalizeLoop : public Pass {
  void canonicalize(LoopInfo *loop);
  void runImpl(Region *region, LoopForest forest);

  bool lcssa;
public:
  CanonicalizeLoop(ModuleOp *module, bool lcssa): Pass(module), lcssa(lcssa) {}

  std::string name() { return "canonicalize-loop"; }
  std::map<std::string, int> stats() { return {}; }
  void run();
};

class LoopRotate : public Pass {
  int rotated = 0;

  void runImpl(LoopInfo *info);
public:
  LoopRotate(ModuleOp *module): Pass(module) {}

  std::string name() { return "loop-rotate"; }
  std::map<std::string, int> stats();
  void run();
};

class LoopUnroll : public Pass {
  int unrolled = 0;

  // Returns true if changed.
  bool runImpl(LoopInfo *info);
public:
  LoopUnroll(ModuleOp *module): Pass(module) {}

  std::string name() { return "loop-unroll"; }
  std::map<std::string, int> stats();
  void run();
};

}

#endif