#include "RvAttrs.h"
#include "RvPasses.h"
#include "RvOps.h"
#include "../codegen/CodeGen.h"
#include "../codegen/Attrs.h"
#include <iostream>
#include <iterator>
#include <vector>

using namespace sys;
using namespace sys::rv;

std::map<std::string, int> RegAlloc::stats() {
  return {
    { "spilled", spilled }
  };
}

// This doesn't invalidate the `Op*` itself, which is crucial.
#define LOWER(Ty, Body) \
  runRewriter(funcOp, [&](Ty *op) { \
    if (op->getOperands().size() == 0) \
      return false; \
    Body \
    op->removeAllOperands(); \
    return true; \
  });

// If assignment does not contain the value, then it doesn't clash with any other one.
// In that case just give it a random register.
#define V(Index, AttrTy) \
  auto v##Index = op->getOperand(Index).defining; \
  op->addAttr<AttrTy>(getReg(v##Index));

#define BINARY V(0, RsAttr) V(1, Rs2Attr)
#define UNARY V(0, RsAttr)

#define REPLACE_BRANCH(T1, T2) \
  REPLACE_BRANCH_IMPL(T1, T2); \
  REPLACE_BRANCH_IMPL(T2, T1)

// Say the before is `blt`, then we might see
//   blt %1 %2 <target = bb1> <else = bb2>
// which means `if (%1 < %2) goto bb1 else goto bb2`.
//
// If the next block is just <bb1>, then we flip it to bge, and make the target <bb2>.
// if the next block is <bb2>, then we make the target <bb2>.
// otherwise, make the target <bb1>, and add another `j <bb2>`.
#define REPLACE_BRANCH_IMPL(BeforeTy, AfterTy) \
  runRewriter(funcOp, [&](BeforeTy *op) { \
    if (!op->hasAttr<ElseAttr>()) \
      return false; \
    auto &target = op->getAttr<TargetAttr>()->bb; \
    auto ifnot = op->getAttr<ElseAttr>()->bb; \
    auto me = op->getParent(); \
    /* If there's no "next block", then give up */ \
    if (me == me->getParent()->getLastBlock()) { \
      GENERATE_J; \
      END_REPLACE; \
    } \
    if (me->nextBlock() == target) { \
      builder.replace<AfterTy>(op, { \
        op->getAttr<RsAttr>(), \
        op->getAttr<Rs2Attr>(), \
        new TargetAttr(ifnot), \
      }); \
      return true; \
    } \
    if (me->nextBlock() == ifnot) { \
      /* No changes needed. */\
      return false; \
    } \
    GENERATE_J; \
    END_REPLACE; \
  })

// Don't touch `target`.
#define GENERATE_J \
  builder.setAfterOp(op); \
  builder.create<JOp>({ new TargetAttr(ifnot) })

#define END_REPLACE \
  op->removeAttr<ElseAttr>(); \
  return true

// In OpBase.cpp
std::ostream &operator<<(std::ostream &os, Value value);

// For debug purposes
void dumpInterf(Region *region, const std::map<Op*, std::set<Op*>> &interf) {
  region->dump(std::cerr, /*depth=*/1);
  std::cerr << "\n\n===== interference graph =====\n\n";
  for (auto [k, v] : interf) {
    std::cerr << k->getResult() << ": ";
    for (auto op : v)
      std::cerr << op->getResult() << " ";
    std::cerr << "\n";
  }
}

void dumpAssignment(Region *region, const std::map<Op*, Reg> &assignment) {
  region->dump(std::cerr, /*depth=*/1);
  std::cerr << "\n\n===== assignment =====\n\n";
  for (auto [k, v] : assignment) {
    std::cerr << k->getResult() << " = " << showReg(v) << "\n";
  }
}

bool hasSpilled(Op *op) {
  if (auto rd = op->findAttr<RdAttr>(); rd && (int) rd->reg < 0)
    return true;
  if (auto rs = op->findAttr<RsAttr>(); rs && (int) rs->reg < 0)
    return true;
  if (auto rs2 = op->findAttr<Rs2Attr>(); rs2 && (int) rs2->reg < 0)
    return true;
  return false;
}

// We use dedicated registers as the "spill" register, for simplicity.
static const Reg spillReg = Reg::s11;
static const Reg spillReg2 = Reg::t6;

// Order for leaf functions. Prioritize temporaries.
static const Reg leafOrder[] = {
  Reg::a0, Reg::a1, Reg::a2, Reg::a3,
  Reg::a4, Reg::a5, Reg::a6, Reg::a7,

  Reg::t0, Reg::t1, Reg::t2, Reg::t3,
  Reg::t4, Reg::t5, // Reg::t6,
  
  Reg::s0, Reg::s1, Reg::s2, Reg::s3, 
  Reg::s4, Reg::s5, Reg::s6, Reg::s7,
  Reg::s8, Reg::s9, Reg::s10, // Reg::s11,
};
// Order for non-leaf functions.
static const Reg normalOrder[] = {
  Reg::s0, Reg::s1, Reg::s2, Reg::s3, 
  Reg::s4, Reg::s5, Reg::s6, Reg::s7,
  Reg::s8, Reg::s9, Reg::s10, // Reg::s11,

  Reg::t0, Reg::t1, Reg::t2, Reg::t3,
  Reg::t4, Reg::t5, // Reg::t6,

  Reg::a0, Reg::a1, Reg::a2, Reg::a3,
  Reg::a4, Reg::a5, Reg::a6, Reg::a7,
};
static const Reg argRegs[] = {
  Reg::a0, Reg::a1, Reg::a2, Reg::a3,
  Reg::a4, Reg::a5, Reg::a6, Reg::a7,
};
static const std::set<Reg> callerSaved = {
  Reg::t0, Reg::t1, Reg::t2, Reg::t3,
  Reg::t4, Reg::t5, Reg::t6,

  Reg::a0, Reg::a1, Reg::a2, Reg::a3,
  Reg::a4, Reg::a5, Reg::a6, Reg::a7,  
};

static const std::set<Reg> calleeSaved = {
  Reg::s0, Reg::s1, Reg::s2, Reg::s3, 
  Reg::s4, Reg::s5, Reg::s6, Reg::s7,
  Reg::s8, Reg::s9, Reg::s10, Reg::s11,
};
constexpr int leafRegCnt = 25;
constexpr int normalRegCnt = 25;

void RegAlloc::runImpl(Region *region, bool isLeaf) {
  const Reg *order = isLeaf ? leafOrder : normalOrder;
  const int regcount = isLeaf ? leafRegCnt : normalRegCnt;

  Builder builder;
  
  std::map<Op*, Reg> assignment;

  auto funcOp = region->getParent();

  // First of all, add 15 precolored placeholders before each call.
  // This denotes that a CallOp clobbers those 15 registers.
  runRewriter(funcOp, [&](CallOp *op) {
    builder.setBeforeOp(op);
    for (auto reg : callerSaved) {
      auto placeholder = builder.create<PlaceHolderOp>();
      assignment[placeholder] = reg;
    }
    // Do it only once.
    return false;
  });

  // Similarly, add placeholders around each GetArg.
  runRewriter(funcOp, [&](GetArgOp *op) {
    auto value = op->getAttr<IntAttr>()->value;
    // TODO: spilling
    assert(value < 8);
    
    // The i'th argument cannot take registers from argReg[i + 1] ~ argReg[argcnt - 1].
    // So we do it like (take `a0` as example):
    //   %0 = placeholder [a1]
    //   %1 = placeholder [a2]
    //   %ARG = getarg <0>
    //   placeholder %0, %1
    // This prevents %ARG being allocated a1 or a2, but doesn't affect any other ops.

    builder.setBeforeOp(op);
    int argcnt = funcOp->getAttr<ArgCountAttr>()->count;
    std::vector<Value> holders;
    for (int i = value + 1; i < std::min(argcnt, 8); i++) {
      auto placeholder = builder.create<PlaceHolderOp>();
      assignment[placeholder] = argRegs[i];
      holders.push_back(placeholder);
    }

    builder.setAfterOp(op);
    builder.create<PlaceHolderOp>(holders);

    builder.replace<ReadRegOp>(op, { new RegAttr(argRegs[value]) });
    return true;
  });

  region->updateLiveness();

  // Interference graph.
  std::map<Op*, std::set<Op*>> interf;

  // Values of readreg, or operands of writereg, or phis (mvs), are prioritzed.
  std::map<Op*, int> priority;
  // The `key` is preferred to have the same value as `value`.
  std::map<Op*, Op*> prefer;

  for (auto bb : region->getBlocks()) {
    // Scan through the block and see the place where the value's last used.
    std::map<Op*, int> lastUsed, defined;
    const auto &ops = bb->getOps();
    auto it = ops.end();
    for (int i = (int) ops.size() - 1; i >= 0; i--) {
      auto op = *--it;
      for (auto v : op->getOperands()) {
        if (!lastUsed.count(v.defining))
          lastUsed[v.defining] = i;
      }
      defined[op] = i;

      // Even though the op is not used, it still lives in the instruction that defines it.
      // Actually this should be eliminated with DCE, but we need to take care of it.
      if (!lastUsed.count(op))
          lastUsed[op] = i + 1;

      // Precolor.
      if (isa<WriteRegOp>(op)) {
        assignment[op] = op->getAttr<RegAttr>()->reg;
        priority[op] = 1;
      }
      if (isa<ReadRegOp>(op)) {
        priority[op] = 1;
      }

      if (isa<PhiOp>(op)) {
        priority[op] = 2;
        for (auto x : op->getOperands()) {
          priority[x.defining] = 1;
          prefer[x.defining] = op;
        }
      }
    }

    // For all liveOuts, they are last-used at place size().
    // If they aren't defined in this block, then `defined[op]` will be zero, which is intended.
    for (auto op : bb->getLiveOut())
      lastUsed[op] = ops.size();

    static auto overlap = [](int b1, int e1, int b2, int e2) {
      // The overlap is of course [max(b1, b2), min(e1, e2)).
      // We check if the range has any elements.
      return std::max(b1, b2) < std::min(e1, e2);
    };

    // If the range [defined, lastUsed) overlap for two variables,
    // then they interfere.
    // Note that if lastUsed == defined then they don't overlap.
    // Also, only checking for `ops` isn't enough. Variables can overlap with liveIns as well.
    for (auto [op1, v1] : lastUsed) {
      for (auto [op2, v2] : lastUsed) {
        if (op1 == op2)
          continue;

        if (overlap(defined[op1], v1, defined[op2], v2)) {
          interf[op1].insert(op2);
          interf[op2].insert(op1);
        }
      }
    }
  }

  // dumpInterf(region, interf);

  // Now time to allocate.

  std::vector<Op*> ops;
  for (auto [k, v] : interf)
    ops.push_back(k);
  // Even though registers in `priority` might not be colliding,
  // we still allocate them here to respect their preference.
  for (auto [k, v] : priority)
    ops.push_back(k);

  // Sort by **descending** degree.
  std::sort(ops.begin(), ops.end(), [&](Op *a, Op *b) {
    auto pa = priority[a];
    auto pb = priority[b];
    return pa == pb ? interf[a].size() > interf[b].size() : pa > pb;
  });

  std::map<Op*, int> spillOffset;
  int currentOffset = 0;
  auto subsp = region->getFirstBlock()->getFirstOp();
  if (isa<SubSpOp>(subsp)) {
    currentOffset = subsp->getAttr<IntAttr>()->value;
  }
  
  for (auto op : ops) {
    std::set<Reg> forbidden;
    for (auto v1 : interf[op]) {
      // In the whole function, `sp` and `zero` are read-only.
      if (assignment.count(v1) && assignment[v1] != Reg::sp && assignment[v1] != Reg::zero)
        forbidden.insert(assignment[v1]);
    }

    if (prefer.count(op)) {
      auto ref = prefer[op];
      // Try to allocate the same register as `ref`.
      if (assignment.count(ref) && !forbidden.count(assignment[ref])) {
        assignment[op] = assignment[ref];
        continue;
      }
    }

    // See if there's any preferred registers.
    int preferred = -1;
    for (auto use : op->getUses()) {
      if (isa<WriteRegOp>(use)) {
        auto reg = use->getAttr<RegAttr>()->reg;
        if (!forbidden.count(reg)) {
          preferred = (int) reg;
          break;
        }
      }
    }
    if (isa<ReadRegOp>(op)) {
      auto reg = op->getAttr<RegAttr>()->reg;
      if (!forbidden.count(reg))
        preferred = (int) reg;
    }

    if (preferred != -1) {
      assignment[op] = (Reg) preferred;
      continue;
    }

    for (int i = 0; i < regcount; i++) {
      if (!forbidden.count(order[i])) {
        assignment[op] = order[i];
        break;
      }
    }

    if (assignment.count(op))
      continue;

    spilled++;
    assignment[op] = (Reg) -(currentOffset += 8);
  }

  // Allocate more stack space for it.
  if (currentOffset != 0) {
    if (isa<SubSpOp>(subsp)) {
      subsp->getAttr<IntAttr>()->value += currentOffset + 8;
    } else {
      builder.setToRegionStart(region);
      builder.create<SubSpOp>({ new IntAttr(currentOffset + 8) });
    }
  }
  
  // dumpAssignment(region, assignment);

  auto getReg = [&](Op *op) {
    return assignment.count(op) ? assignment[op] : order[0];
  };

  // Convert all operands to registers.
  LOWER(AddOp, BINARY);
  LOWER(AddwOp, BINARY);
  LOWER(SubOp, BINARY);
  LOWER(SubwOp, BINARY);
  LOWER(MulwOp, BINARY);
  LOWER(MulhOp, BINARY);
  LOWER(MulhuOp, BINARY);
  LOWER(MulOp, BINARY);
  LOWER(DivwOp, BINARY);
  LOWER(DivOp, BINARY);
  LOWER(BneOp, BINARY);
  LOWER(BeqOp, BINARY);
  LOWER(BltOp, BINARY);
  LOWER(BgeOp, BINARY);
  LOWER(StoreOp, BINARY);
  LOWER(XorOp, BINARY);
  LOWER(SltOp, BINARY);
  
  LOWER(LoadOp, UNARY);
  LOWER(AddiwOp, UNARY);
  LOWER(AddiOp, UNARY);
  LOWER(SlliwOp, UNARY);
  LOWER(SrliwOp, UNARY);
  LOWER(SraiwOp, UNARY);
  LOWER(SraiOp, UNARY);
  LOWER(SeqzOp, UNARY);
  LOWER(SnezOp, UNARY);
  LOWER(SltiOp, UNARY);

  // Remove all operands of calls and returns.
  // The "operands" are only formal and carry no real meaning.
  runRewriter(funcOp, [&](CallOp *op) {
    if (!op->getOperands().size())
      return false;

    op->removeAllOperands();
    return true;
  });

  runRewriter(funcOp, [&](RetOp *op) {
    if (!op->getOperands().size())
      return false;
    
    op->removeAllOperands();
    return true;
  });

  // Remove placeholders inserted previously.
  auto holders = funcOp->findAll<PlaceHolderOp>();
  for (auto holder : holders)
    holder->removeAllOperands();
  for (auto holder : holders)
    holder->erase();

  //   writereg %1, <reg = a0>
  // becomes
  //   mv a0, assignment[%1]
  // As RdAttr is supplied, though `assignment[]` won't have the new op recorded, it's fine.
  runRewriter(funcOp, [&](WriteRegOp *op) {
    auto rd = new RdAttr(op->getAttr<RegAttr>()->reg);
    auto rs = new RsAttr(getReg(op->getOperand(0).defining));
    builder.replace<MvOp>(op, { rd, rs });
    return true;
  });

  //   readreg %1, <reg = a0>
  // becomes
  //   mv assignment[%1], a0
  // Note that rd will be placed later. We must update assignment.
  runRewriter(funcOp, [&](ReadRegOp *op) {
    auto rs = new RsAttr(op->getAttr<RegAttr>()->reg);
    // Now a new MvOp is constructed. Update it in assignment[].
    auto rd = getReg(op);
    auto mv = builder.replace<MvOp>(op, { rs });
    assignment[mv] = rd;
    return true;
  });

  // Finally, after everything has been erased:
  // Destruct phi.
  std::vector<Op*> allPhis;
  auto bbs = region->getBlocks();

  // Split edges.
  for (auto bb : bbs) {
    // If a block has multiple successors with phi, then we split the edges. As an example:
    // 
    // bb0:
    //   %0 = ...
    //   %1 = ...
    //   br %1 <bb1> <bb2>
    // bb1:
    //   phi %2, %0, ...
    // bb2:
    //   phi %3, %0, ...
    //
    // If we naively create a move at the end of bb0, then it's wrong.
    // We need to rewrite it into
    //
    // bb0:
    //   br %1 <bb3> <bb4>
    // bb3:
    //   j bb1
    // bb4:
    //   j bb2
    // ...
    //
    // To actually make it work.
    if (bb->getSuccs().size() <= 1)
      continue;

    // Note that we need to split even if there's no phi in one of the blocks.
    // This is because the registers of branch operation can be clobbered if that's not done.
    // Consider:
    //   b %1, <bb1>, <bb2>
    // bb1:
    //   %3 = phi ...
    // It is entirely possible for %3 to have the same register as %1.
    
    auto edge1 = region->insertAfter(bb);
    auto edge2 = region->insertAfter(bb);
    auto bbTerm = bb->getLastOp();

    // Create edge for target branch.
    auto target = bbTerm->getAttr<TargetAttr>();
    auto oldTarget = target->bb;
    target->bb = edge1;

    builder.setToBlockEnd(edge1);
    builder.create<JOp>({ new TargetAttr(oldTarget) });

    // Create edge for else branch.
    auto ifnot = bbTerm->getAttr<ElseAttr>();
    auto oldElse = ifnot->bb;
    ifnot->bb = edge2;

    builder.setToBlockEnd(edge2);
    builder.create<JOp>({ new TargetAttr(oldElse) });

    // Rename the blocks of the phis.
    for (auto succ : bb->getSuccs()) {
      for (auto phis : succ->getPhis()) {
        for (auto attr : phis->getAttrs()) {
          auto from = cast<FromAttr>(attr);
          if (from->bb != bb)
            continue;
          if (succ == oldTarget)
            from->bb = edge1;
          if (succ == oldElse)
            from->bb = edge2;
        }
      }
    }
  }

  for (auto bb : bbs) {
    auto phis = bb->getPhis();

    std::vector<Op*> moves;
    for (auto phi : phis) {
      auto &ops = phi->getOperands();
      auto &attrs = phi->getAttrs();
      for (size_t i = 0; i < ops.size(); i++) {
        auto bb = cast<FromAttr>(attrs[i])->bb;
        auto terminator = *--bb->getOps().end();
        builder.setBeforeOp(terminator);
        auto mv = builder.create<MvOp>({
          new RdAttr(getReg(phi)),
          new RsAttr(getReg(ops[i].defining))
        });
        moves.push_back(mv);
      }
    }

    // Detect circular copies.
    std::map<Reg, Reg> moveMap;
    std::map<std::pair<Reg, Reg>, Op*> moveOpMap;
    std::set<Reg> srcs;
    std::set<Reg> dsts;

    for (auto mv : moves) {
      auto src = mv->getAttr<RdAttr>()->reg;
      auto dst = mv->getAttr<RsAttr>()->reg;
      moveMap[src] = dst;
      moveOpMap[std::pair {src, dst}] = mv;
      srcs.insert(src);
      dsts.insert(dst);
    }

    // Registers that currently hold valid values.
    std::set<Reg> valid = srcs;

    std::set<Reg> visited;
    // Unwanted moves.
    std::vector<Op*> toErase;

    // Detect cycles.
    for (auto mv : moves) {
      auto src = mv->getAttr<RdAttr>()->reg;
      auto dst = mv->getAttr<RsAttr>()->reg;
      if (visited.count(src))
        continue;

      std::vector<Reg> chain;
      std::vector<Op*> ops;

      Reg cur = src;

      // Walk the chain of copying.
      while (moveMap.count(cur) && !visited.count(cur)) {
        visited.insert(cur);
        chain.push_back(cur);
        ops.push_back(moveOpMap[std::pair { cur, moveMap[cur] }]);
        cur = moveMap[cur];
      }

      if (chain.size() <= 1 || cur != chain.front())
        continue;

      // The chain is actually a cycle.
      // It has to have at least 2 arguments (as self-loop is not acceptable).
      std::cerr << "remark: cycle detected\n";

      // Break cycle using a temp register, say the spill register s11.
      Reg first = chain.front();
      builder.setBeforeOp(mv);
      builder.create<MvOp>({
        new RdAttr(spillReg),
        new RsAttr(first)
      });

      for (size_t i = 1; i < chain.size(); ++i) {
        builder.create<MvOp>({
          new RdAttr(chain[i - 1]),
          new RsAttr(chain[i])
        });
      }

      builder.create<MvOp>({
        new RdAttr(chain.back()),
        new RsAttr(spillReg)
      });

      // The original moves are now for removal.
      std::copy(ops.begin(), ops.end(), std::back_inserter(toErase));
    }

    for (auto mv : toErase)
      mv->erase();

    // Copy the local phis into `allPhis` for removal.
    std::copy(phis.begin(), phis.end(), std::back_inserter(allPhis));
  }

  // Erase all phi's properly. There might be cross-reference across blocks.
  // So we need to remove all operands first.
  for (auto phi : allPhis)
    phi->removeAllOperands();

  for (auto phi : allPhis)
    phi->erase();

  for (auto bb : region->getBlocks()) {
    for (auto op : bb->getOps()) {
      if (hasRd(op) && !op->hasAttr<RdAttr>())
        op->addAttr<RdAttr>(getReg(op));
    }
  }

  // Now let's deal with spilled registers.
  for (auto bb : region->getBlocks()) {
    std::vector<Op*> spilling;
    for (auto op : bb->getOps()) {
      if (hasSpilled(op))
        spilling.push_back(op);
    }

    for (auto op : spilling) {
      if (auto rs = op->findAttr<RsAttr>(); rs && (int) rs->reg < 0) {
        int offset = -(int) rs->reg;

        builder.setBeforeOp(op);
        if (offset < 2048) {
          //   op rd, <spilled>
          // becomes
          //   ld s11, offset(sp)
          //   op rd, s11
          builder.create<LoadOp>({
            new RdAttr(spillReg),
            new RsAttr(Reg::sp),
            new IntAttr(offset),
            new SizeAttr(8)
          });
        } else {
          // Cannot build a single load.
          //
          //   op rd, <spilled>
          // becomes
          //   li s11, offset
          //   add s11, s11, sp
          //   ld s11, 0(s11)
          //   op rd, s11
          builder.create<LiOp>({
            new RdAttr(spillReg),
            new IntAttr(offset)
          });
          builder.create<AddOp>({
            new RdAttr(spillReg),
            new RsAttr(spillReg),
            new Rs2Attr(Reg::sp)
          });
          builder.create<LoadOp>({
            new RdAttr(spillReg),
            new RsAttr(spillReg),
            new IntAttr(0),
            new SizeAttr(8)
          });
        }
        rs->reg = spillReg;
      }

      if (auto rs2 = op->findAttr<Rs2Attr>(); rs2 && (int) rs2->reg < 0) {
        int offset = -(int) rs2->reg;

        builder.setBeforeOp(op);
        // Similar to the sequence above.
        if (offset < 2048) {
          builder.create<LoadOp>({
            new RdAttr(spillReg2),
            new RsAttr(Reg::sp),
            new IntAttr(offset),
            new SizeAttr(8)
          });
        } else {
          builder.create<LiOp>({
            new RdAttr(spillReg2),
            new IntAttr(offset)
          });
          builder.create<AddOp>({
            new RdAttr(spillReg2),
            new RsAttr(spillReg2),
            new Rs2Attr(Reg::sp)
          });
          builder.create<LoadOp>({
            new RdAttr(spillReg2),
            new RsAttr(spillReg2),
            new IntAttr(0),
            new SizeAttr(8)
          });
        }
        rs2->reg = spillReg2;
      }

      if (auto rd = op->findAttr<RdAttr>(); rd && (int) rd->reg < 0) {
        int offset = -(int) rd->reg;

        builder.setAfterOp(op);
        if (offset < 2048) {
          //   op <spilled>, rs
          // becomes
          //   op s11, rs
          //   sd s11, offset(sp)
          builder.create<StoreOp>({
            new RsAttr(spillReg),
            new Rs2Attr(Reg::sp),
            new IntAttr(offset),
            new SizeAttr(8)
          });
        } else {
          // Note that we can't tamper spillReg this time,
          // but we still have spillReg2.
          //
          //   op <spilled>, rs
          // becomes
          //   op s11, rs
          //   li t6, offset
          //   addi t6, t6, sp
          //   sd s11, 0(t6)
          builder.create<LiOp>({
            new RdAttr(spillReg2),
            new IntAttr(offset)
          });
          builder.create<AddOp>({
            new RdAttr(spillReg2),
            new RsAttr(spillReg2),
            new Rs2Attr(Reg::sp)
          });
          builder.create<StoreOp>({
            new RsAttr(spillReg),
            new Rs2Attr(spillReg2),
            new IntAttr(0),
            new SizeAttr(8)
          });
        }
        rd->reg = spillReg;
      }
    }
  }
}

void RegAlloc::tidyup(Region *region) {
  Builder builder;
  auto funcOp = region->getParent();
  region->updatePreds();

  // Replace blocks with only a single `j` as terminator.
  std::map<BasicBlock*, BasicBlock*> jumpTo;
  for (auto bb : region->getBlocks()) {
    if (bb->getOps().size() == 1 && isa<JOp>(bb->getLastOp())) {
      auto target = bb->getLastOp()->getAttr<TargetAttr>()->bb;
      jumpTo[bb] = target;
    }
  }

  // Calculate jump-to closure.
  for (auto &[k, v] : jumpTo) {
    if (jumpTo.count(v))
      v = jumpTo[v];
  }

  for (auto bb : region->getBlocks()) { 
    auto term = bb->getLastOp();
    if (auto target = term->findAttr<TargetAttr>()) {
      if (jumpTo.count(target->bb))
        target->bb = jumpTo[target->bb];
    }

    if (auto ifnot = term->findAttr<ElseAttr>()) {
      if (jumpTo.count(ifnot->bb))
        ifnot->bb = jumpTo[ifnot->bb];
    }
  }

  // Erase all those single-j's.
  region->updatePreds();
  for (auto [bb, v] : jumpTo)
    bb->erase();

  // Now branches are still having both TargetAttr and ElseAttr.
  // Replace them (perform split when necessary), so that they only have one target.
  REPLACE_BRANCH(BltOp, BgeOp);
  REPLACE_BRANCH(BeqOp, BneOp);

  // Also, eliminate useless JOp.
  runRewriter(funcOp, [&](JOp *op) {
    auto &target = op->getAttr<TargetAttr>()->bb;
    auto me = op->getParent();
    if (me == me->getParent()->getLastBlock())
      return false;

    if (me->nextBlock() == target) {
      op->erase();
      return true;
    }
    return false;
  });

  // Eliminate useless MvOp.
  runRewriter(funcOp, [&](MvOp *op) {
    if (op->getAttr<RdAttr>()->reg == op->getAttr<RsAttr>()->reg) {
      op->erase();
      return true;
    }
    return false;
  });
}

void save(Builder builder, const std::vector<Reg> &regs, int offset) {
  for (auto reg : regs) {
    offset -= 8;
    if (offset < 2048) {
      builder.create<sys::rv::StoreOp>({
        /*value=*/new RsAttr(reg),
        /*addr=*/new Rs2Attr(Reg::sp),
        /*offset=*/new IntAttr(offset),
        /*size=*/new SizeAttr(8)
      });
    } else {
      // li s11, offset
      // addi s11, s11, sp
      // sd reg, 0(s11)
      builder.create<LiOp>({
        new RdAttr(spillReg),
        new IntAttr(offset)
      });
      builder.create<AddOp>({
        new RdAttr(spillReg),
        new RsAttr(spillReg),
        new Rs2Attr(Reg::sp)
      });
      builder.create<sys::rv::StoreOp>({
        new RsAttr(reg),
        new Rs2Attr(spillReg),
        new IntAttr(0),
        new SizeAttr(8)
      });
    }
  }
}

void load(Builder builder, const std::vector<Reg> &regs, int offset) {
  for (auto reg : regs) {
    offset -= 8;
    if (offset < 2048) {
      builder.create<sys::rv::LoadOp>({
        /*value=*/new RdAttr(reg),
        /*addr=*/new RsAttr(Reg::sp),
        /*offset=*/new IntAttr(offset),
        /*size=*/new SizeAttr(8)
      });
    } else {
      // li s11, offset
      // addi s11, s11, sp
      // ld reg, 0(s11)
      builder.create<LiOp>({
        new RdAttr(spillReg),
        new IntAttr(offset)
      });
      builder.create<AddOp>({
        new RdAttr(spillReg),
        new RsAttr(spillReg),
        new Rs2Attr(Reg::sp)
      });
      builder.create<sys::rv::LoadOp>({
        new RdAttr(reg),
        new RsAttr(spillReg),
        new IntAttr(0),
        new SizeAttr(8)
      });
    }
  }
}

void RegAlloc::proEpilogue(FuncOp *funcOp, bool isLeaf) {
  Builder builder;
  auto usedRegs = usedRegisters[funcOp];
  auto region = funcOp->getRegion();

  // Preserve return address if this calls another function.
  std::vector<Reg> preserve;
  for (auto x : usedRegs) {
    if (calleeSaved.count(x))
      preserve.push_back(x);
  }
  if (!isLeaf)
    preserve.push_back(Reg::ra);

  // If there's a SubSpOp, then it must be at the top of the first block.
  auto op = region->getFirstBlock()->getFirstOp();
  int offset = 0;
  if (isa<SubSpOp>(op)) {
    offset = op->getAttr<IntAttr>()->value;
    op->removeAttr<IntAttr>();
    op->addAttr<IntAttr>(offset += 8 * preserve.size());
  } else if (!preserve.empty()) {
    builder.setToRegionStart(region);
    op = builder.create<SubSpOp>({ new IntAttr(offset += 8 * preserve.size()) });
  }

  // Round op to the nearest multiple of 16.
  // This won't be entered in the special case where offset == 0.
  if (offset % 16 != 0) {
    int &value = op->getAttr<IntAttr>()->value;
    offset = value = offset / 16 * 16 + 16;
  }

  // Add function prologue, preserving the regs.
  builder.setAfterOp(op);
  save(builder, preserve, offset);

  // Similarly add function epilogue.
  if (offset != 0) {
    auto rets = funcOp->findAll<RetOp>();
    auto bb = region->appendBlock();
    for (auto ret : rets)
      builder.replace<JOp>(ret, { new TargetAttr(bb) });

    builder.setToBlockStart(bb);

    load(builder, preserve, offset);
    builder.create<SubSpOp>({ new IntAttr(-offset) });
    builder.create<RetOp>();
  }

  // For each call, preserve caller-preserved ones.
  std::vector<Reg> callPreserve;
  for (auto x : usedRegs) {
    if (callerSaved.count(x))
      callPreserve.push_back(x);
  }

  if (!callPreserve.empty()) {
    auto calls = funcOp->findAll<CallOp>();
    for (auto call : calls) {
      std::vector<Reg> caller;
      auto callName = call->getAttr<NameAttr>()->name;
      // Surely we don't need to preserve arguments.
      // TODO: handle this.
      break;
      
      if (isExtern(callName))
        caller = callPreserve;
      else {
        // If we know what registers that function would use,
        // then we only need to preserve those registers that get tampered with.
        auto calledFunc = findFunction(callName);
        for (auto r : callPreserve) {
          if (usedRegisters[calledFunc].count(r))
            caller.push_back(r);
        }
      }

      int offset = caller.size() * 4;

      builder.setBeforeOp(call);
      builder.create<SubSpOp>({ new IntAttr(offset) });
      save(builder, caller, offset);

      builder.setAfterOp(call);
      load(builder, caller, offset);
      builder.create<SubSpOp>({ new IntAttr(-offset) });
    }
  }

  //   subsp <4>
  // becomes
  //   addi <rd = sp> <rs = sp> <-4>
  runRewriter(funcOp, [&](SubSpOp *op) {
    int offset = op->getAttr<IntAttr>()->value;
    if (offset <= 2048 && offset > -2048) {
      builder.replace<AddiOp>(op, {
        new RdAttr(Reg::sp),
        new RsAttr(Reg::sp),
        new IntAttr(-offset)
      });
    } else {
      builder.setBeforeOp(op);
      builder.create<LiOp>({
        new RdAttr(Reg::t0),
        new IntAttr(offset)
      });
      builder.replace<SubOp>(op, {
        new RdAttr(Reg::sp),
        new RsAttr(Reg::sp),
        new Rs2Attr(Reg::t0)
      });
    }
    return true;
  });
}

void RegAlloc::run() {
  auto funcs = module->findAll<FuncOp>();
  std::set<FuncOp*> leaves;

  for (auto func : funcs) {
    auto calls = func->findAll<sys::rv::CallOp>();
    if (calls.size() == 0)
      leaves.insert(func);
    runImpl(func->getRegion(), calls.size() == 0);
  }

  // Have a look at what registers are used inside each function.
  for (auto func : funcs) {
    auto &set = usedRegisters[func];
    for (auto bb : func->getRegion()->getBlocks()) {
      for (auto op : bb->getOps()) {
        if (op->hasAttr<RdAttr>())
          set.insert(op->getAttr<RdAttr>()->reg);
        if (op->hasAttr<RsAttr>())
          set.insert(op->getAttr<RsAttr>()->reg);
        if (op->hasAttr<Rs2Attr>())
          set.insert(op->getAttr<Rs2Attr>()->reg);
      }
    }
  }

  for (auto func : funcs) {
    proEpilogue(func, leaves.count(func));
    tidyup(func->getRegion());
  }
}
