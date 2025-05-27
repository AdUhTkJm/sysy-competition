#ifndef LOOP_PASSES_H
#define LOOP_PASSES_H

#include "Pass.h"
#include "../codegen/CodeGen.h"
#include "../codegen/Attrs.h"

#include <unordered_set>
#include <set>

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
  BasicBlock *preheader = nullptr;
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
  auto getExit() const { assert(exits.size() == 1); return *exits.begin(); }
  auto getInduction() const { return induction; }
  auto getStart() const { return start; }
  auto getStop() const { return stop; }
  int getStep() const { return step; }

  bool contains(const BasicBlock *bb) const { return bbs.count(const_cast<BasicBlock*>(bb)); }

  // Note that this relies on a previous dump of the parent op,
  // otherwise the numbers of blocks are meaningless.
  void dump(std::ostream &os = std::cerr);
};

inline std::ostream &operator<<(std::ostream &os, LoopInfo *info) {
  info->dump(os);
  return os;
}

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
  ~LoopAnalysis();

  std::string name() override { return "loop-analysis"; }
  std::map<std::string, int> stats() override { return {}; }
  LoopForest runImpl(Region *region);
  void run() override;
  void reset() { info = {}; }

  auto getResult() { return info; }
};

// Canonicalize loops. Ensures:
//   1) A single preheader;
//   2) In LCSSA, if it's constructed with `lcssa = true`.
class CanonicalizeLoop : public Pass {
  void canonicalize(LoopInfo *loop);
  void runImpl(Region *region, LoopForest forest);

  bool lcssa;
public:
  CanonicalizeLoop(ModuleOp *module, bool lcssa): Pass(module), lcssa(lcssa) {}

  std::string name() override { return "canonicalize-loop"; }
  std::map<std::string, int> stats() override { return {}; }
  void run() override;
};

class LoopRotate : public Pass {
  int rotated = 0;

  void runImpl(LoopInfo *info);
public:
  LoopRotate(ModuleOp *module): Pass(module) {}

  std::string name() override { return "loop-rotate"; }
  std::map<std::string, int> stats() override;
  void run() override;
};

class ConstLoopUnroll : public Pass {
  std::map<Op*, Op*> phiMap;
  std::map<Op*, Op*> exitlatch;
  int unrolled = 0;

  // Returns true if changed.
  bool runImpl(LoopInfo *info);
  // Returns the new latch.
  // Starts insertion after `bb`, and duplicate `info` a total of `unroll` times.
  // This only unrolls constant loops.
  BasicBlock *copyLoop(LoopInfo *info, BasicBlock *bb, int unroll);
public:
  ConstLoopUnroll(ModuleOp *module): Pass(module) {}

  std::string name() override { return "const-loop-unroll"; }
  std::map<std::string, int> stats() override;
  void run() override;
};

class LoopUnswitch : public Pass {
  int unswitched = 0;
public:
  LoopUnswitch(ModuleOp *module): Pass(module) {}

  std::string name() override { return "loop-unswitch"; }
  std::map<std::string, int> stats() override;
  void run() override;
};

class SCEV : public Pass {
  int expanded = 0;

  // All addresses stored inside current loop.
  // We need this because we need to find variants as well,
  // but it's slightly different from what LICM requires.
  std::vector<Op*> stores;
  std::unordered_map<Op*, Op*> start;
  std::unordered_set<Op*> nochange;

  DomTree domtree;
  bool impure;

  void rewrite(BasicBlock *bb, LoopInfo *info);
  void runImpl(LoopInfo *info);
  void discardIv(LoopInfo *info);
public:
  SCEV(ModuleOp *module): Pass(module) {}

  std::string name() override { return "scev"; }
  std::map<std::string, int> stats() override;
  void run() override;
};

class LICM : public Pass {
  int hoisted = 0;
  DomTree domtree;
  // All addresses stored inside current loop.
  std::vector<Op*> stores;
  // Whether the current function has an impure call.
  bool impure;

  // A store is hoistable when no branch or load has been met.
  void markVariant(LoopInfo *info, BasicBlock *bb, bool hoistable);
  void runImpl(LoopInfo *info);
public:
  LICM(ModuleOp *module): Pass(module) {}

  std::string name() override { return "licm"; }
  std::map<std::string, int> stats() override;
  void run() override;
};

}

#endif