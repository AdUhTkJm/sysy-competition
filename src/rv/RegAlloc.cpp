#include "RvAttrs.h"
#include "RvPasses.h"
#include "RvOps.h"
#include "../codegen/CodeGen.h"
#include "../codegen/Attrs.h"

using namespace sys;
using namespace sys::rv;

std::map<std::string, int> RegAlloc::stats() {
  return {
    { "spilled", spilled }
  };
}

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
      target = ifnot; \
      END_REPLACE; \
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

bool hasRd(Op *op) {
  return 
    isa<AddOp>(op) ||
    isa<SubOp>(op) ||
    isa<MulOp>(op) ||
    isa<DivOp>(op) ||
    isa<sys::rv::LoadOp>(op) ||
    isa<AddiOp>(op) ||
    isa<LiOp>(op) ||
    isa<MvOp>(op) ||
    isa<ReadRegOp>(op);
}

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

void RegAlloc::runImpl(Region *region, bool isLeaf) {
  region->updateLiveness();

  // Produce interference graph.
  std::map<Op*, std::set<Op*>> interf;

  for (auto bb : region->getBlocks()) {
    // All live-ins must intefere with each other.
    for (auto v1 : bb->getLiveIn()) {
      for (auto v2 : bb->getLiveIn()) {
        if (v1 != v2) {
          interf[v1].insert(v2);
          interf[v2].insert(v1);
        }
      }
    }

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
    }

    // For all liveIns, they are defined at place -1.
    for (auto op : bb->getLiveIn())
      defined[op] = -1;

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

  // Order for leaf functions. Prioritize temporaries.
  static const Reg leafOrder[] = {
    Reg::t0, Reg::t1, Reg::t2, Reg::t3,
    Reg::t4, Reg::t5, Reg::t6,

    Reg::a0, Reg::a1, Reg::a2, Reg::a3,
    Reg::a4, Reg::a5, Reg::a6, Reg::a7,
    
    Reg::s0, Reg::s1, Reg::s2, Reg::s3, 
    Reg::s4, Reg::s5, Reg::s6, Reg::s7,
    Reg::s8, Reg::s9, Reg::s10, Reg::s11,
  };
  // Order for non-leaf functions.
  static const Reg normalOrder[] = {
    Reg::s0, Reg::s1, Reg::s2, Reg::s3, 
    Reg::s4, Reg::s5, Reg::s6, Reg::s7,
    Reg::s8, Reg::s9, Reg::s10, Reg::s11,

    Reg::t0, Reg::t1, Reg::t2, Reg::t3,
    Reg::t4, Reg::t5, Reg::t6,

    Reg::a0, Reg::a1, Reg::a2, Reg::a3,
    Reg::a4, Reg::a5, Reg::a6, Reg::a7,
  };
  const Reg *order = isLeaf ? leafOrder : normalOrder;
  constexpr int regcount = 27;

  std::vector<Op*> ops;
  for (auto [k, v] : interf)
    ops.push_back(k);

  // Sort by **descending** degree.
  std::sort(ops.begin(), ops.end(), [&](Op *a, Op *b) {
    return interf[a].size() > interf[b].size();
  });
  
  std::map<Op*, Reg> assignment;

  for (auto op : ops) {
    std::set<Reg> forbidden;
    for (auto v1 : interf[op]) {
      if (assignment.count(v1))
        forbidden.insert(assignment[v1]);
    }

    // See if there's any preferred registers.
    int preferred = -1;
    for (auto use : op->getUses()) {
      if (isa<ReadRegOp>(use) || isa<WriteRegOp>(use)) {
        auto reg = use->getAttr<RegAttr>()->reg;
        if (!forbidden.count(reg)) {
          preferred = (int) reg;
          break;
        }
      }
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

    // Spilled. Not handled yet.
    spilled++;
    assert(false);
  }
  
  // dumpAssignment(region, assignment);

  Builder builder;

  std::set<PhiOp*> phisToErase;

  // Destruct phis before any other lowering happens.
  runRewriter([&](PhiOp *op) {
    if (phisToErase.count(op))
      return false;

    phisToErase.insert(op);

    // Note that Mem2Reg can produce duplicate operands.
    std::set<Value> values;
    for (auto operand : op->getOperands()) {
      if (values.count(operand))
        continue;

      values.insert(operand);

      // Find the block of the operand, and stick a MvOp at the end, 
      // before the terminator (`j`, `beq` etc.)
      auto src = operand.defining;
      auto bb = src->getParent();
      auto terminator = *--bb->getOps().end();
      builder.setBeforeOp(terminator);
      builder.create<MvOp>({
        new RdAttr(assignment[op]),
        new RsAttr(assignment[src]),
      });
    }
    return true;
  });

  auto funcOp = region->getParent();

  auto getReg = [&](Op *op) {
    return assignment.count(op) ? assignment[op] : order[0];
  };

  // Convert all operands to registers.
  LOWER(AddOp, BINARY);
  LOWER(SubOp, BINARY);
  LOWER(MulOp, BINARY);
  LOWER(DivOp, BINARY);
  LOWER(BneOp, BINARY);
  LOWER(BeqOp, BINARY);
  LOWER(BltOp, BINARY);
  LOWER(BgeOp, BINARY);
  LOWER(StoreOp, BINARY);
  LOWER(BnezOp, UNARY);
  LOWER(BezOp, UNARY);
  LOWER(AddiOp, UNARY);
  LOWER(LoadOp, UNARY);

  //   writereg %1, <reg = a0>
  // becomes
  //   mv a0, assignment[%1]
  runRewriter(funcOp, [&](WriteRegOp *op) {
    auto rd = new RdAttr(op->getAttr<RegAttr>()->reg);
    auto rs = new RsAttr(getReg(op->getOperand(0).defining));
    builder.replace<MvOp>(op, { rd, rs });
    return true;
  });

  //   readreg %1, <reg = a0>
  // becomes
  //   mv assignment[%1], a0
  // Note that rd will be placed later.
  runRewriter(funcOp, [&](ReadRegOp *op) {
    auto rs = new RsAttr(op->getAttr<RegAttr>()->reg);
    builder.replace<MvOp>(op, { rs });
    return true;
  });

  //   subsp <4>
  // becomes
  //   addi <rd = sp> <rs = sp> <-4>
  runRewriter(funcOp, [&](SubSpOp *op) {
    int offset = op->getAttr<IntAttr>()->value;
    builder.replace<AddiOp>(op, {
      new RdAttr(Reg::sp),
      new RsAttr(Reg::sp),
      new IntAttr(offset)
    });
    return true;
  });

  for (auto bb : region->getBlocks()) {
    for (auto op : bb->getOps()) {
      auto result = op->getResult().defining;
      if (hasRd(op) && !op->hasAttr<RdAttr>())
        op->addAttr<RdAttr>(getReg(result));
    }
  }

  // Remove all phi's. They're destructed beforehand.
  for (auto phi : phisToErase)
    phi->erase();

  // Now branches are still having both TargetAttr and ElseAttr.
  REPLACE_BRANCH(BltOp, BgeOp);
  REPLACE_BRANCH(BeqOp, BneOp);
  REPLACE_BRANCH(BnezOp, BezOp);
}

void RegAlloc::run() {
  auto funcs = module->findAll<FuncOp>();

  for (auto func : funcs) {
    auto calls = func->findAll<sys::rv::CallOp>();
    runImpl(func->getRegion(), calls.size() == 0);
  }
}
