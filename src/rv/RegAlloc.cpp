#include "RvAttrs.h"
#include "RvPasses.h"
#include "RvOps.h"
#include "../codegen/CodeGen.h"
#include "../codegen/Attrs.h"
#include <iostream>

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

// In OpBase.cpp
std::ostream &operator<<(std::ostream &os, Value value);

// For debug purposes
void dumpInterf(Region *region, const std::map<Op*, std::set<Op*>> &interf, const std::map<Op*, std::set<Reg>> &forbidden) {
  region->dump(std::cerr, /*depth=*/1);
  std::cerr << "\n\n===== interference graph =====\n\n";
  for (auto [k, v] : interf) {
    std::cerr << k->getResult() << ": ";
    for (auto op : v)
      std::cerr << op->getResult() << " ";
    std::cerr << "\n";
  }
  std::cerr << "\n\n===== forbidden regs =====\n\n";
  for (auto [k, v] : forbidden) {
    std::cerr << k->getResult() << ": ";
    for (auto reg : v)
      std::cerr << showReg(reg) << " ";
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

// Order for leaf functions. Prioritize temporaries.
static const Reg leafOrder[] = {
  Reg::a0, Reg::a1, Reg::a2, Reg::a3,
  Reg::a4, Reg::a5, Reg::a6, Reg::a7,

  Reg::t0, Reg::t1, Reg::t2, Reg::t3,
  Reg::t4, Reg::t5, Reg::t6,
  
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
constexpr int regcount = 27;

void RegAlloc::runImpl(Region *region, bool isLeaf) {
  const Reg *order = isLeaf ? leafOrder : normalOrder;

  Builder builder;

  region->updateLiveness();

  // Produce interference graph.
  std::map<Op*, std::set<Op*>> interf;
  std::map<Op*, std::set<Reg>> indivForbidden;

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

      // Might need to deal with this later, about regDefined etc.
      if (isa<WriteRegOp>(op)) {
        priority[op->getOperand().defining] = 1;
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

  // dumpInterf(region, interf, indivForbidden);

  // Now time to allocate.

  std::vector<Op*> ops;
  for (auto [k, v] : interf)
    ops.push_back(k);

  // Sort by **descending** degree.
  std::sort(ops.begin(), ops.end(), [&](Op *a, Op *b) {
    auto pa = priority[a];
    auto pb = priority[b];
    return pa == pb ? interf[a].size() > interf[b].size() : pa > pb;
  });
  
  std::map<Op*, Reg> assignment;

  for (auto op : ops) {
    std::set<Reg> forbidden = indivForbidden[op];
    for (auto v1 : interf[op]) {
      if (assignment.count(v1))
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

    // Spilled. Not handled yet.
    spilled++;
    assert(false);
  }
  
  // dumpAssignment(region, assignment);

  auto funcOp = region->getParent();

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
  
  LOWER(BnezOp, UNARY);
  LOWER(BezOp, UNARY);
  LOWER(LoadOp, UNARY);
  LOWER(AddiwOp, UNARY);
  LOWER(AddiOp, UNARY);
  LOWER(SlliwOp, UNARY);
  LOWER(SrliwOp, UNARY);
  LOWER(SraiwOp, UNARY);
  LOWER(SraiOp, UNARY);
  LOWER(SeqzOp, UNARY);
  LOWER(SnezOp, UNARY);

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
  std::vector<PhiOp*> phis;
  runRewriter(funcOp, [&](PhiOp *op) {
    auto &ops = op->getOperands();
    auto &froms = op->getAttrs();
    for (size_t i = 0; i < ops.size(); i++) {
      // Find the block of the operand, and stick an Op at the end, 
      // before the terminator (`j`, `beq` etc.)
      auto src = ops[i].defining;
      if (isa<PhiOp>(src) && src->getParent() == op->getParent() && src != op) {
        // The swapping problem. Not sure how to deal with it.
        std::cerr << "swapping\n";
        assert(false);
      }
      auto bb = cast<FromAttr>(froms[i])->bb;
      auto terminator = *--bb->getOps().end();
      builder.setBeforeOp(terminator);
      builder.create<MvOp>({
        new RdAttr(getReg(op)),
        new RsAttr(getReg(src))
      });
    }
    // Cannot erase here, in case another phi refers to this phi.
    phis.push_back(op);
    // Return false so that each phi will only be processed once.
    return false;
  });

  bool changed;
  do {
    changed = false;
    std::vector<PhiOp*> newPhis;
    for (auto x : phis) {
      if (x->getUses().size() == 0) {
        x->erase();
        changed = true;
        continue;
      }
      newPhis.push_back(x);
    }
    phis = newPhis;
  } while (changed);
  
  if (!phis.empty()) {
    std::cerr << "=== bad uses of phi ===\n";
    funcOp->dump(std::cerr);
    for (auto op : phis) {
      // All ops should have been lowered.
      // If not, try print out what happened for debugging.
      if (op->getUses().size() > 0) {
        for (auto use : op->getUses())
          use->dump(std::cerr);
      }
    }
  }

  for (auto bb : region->getBlocks()) {
    for (auto op : bb->getOps()) {
      if (hasRd(op) && !op->hasAttr<RdAttr>())
        op->addAttr<RdAttr>(getReg(op));
    }
  }
}

void RegAlloc::tidyup(Region *region) {
  Builder builder;
  auto funcOp = region->getParent();

  // Now branches are still having both TargetAttr and ElseAttr.
  // Replace them (perform split when necessary), so that they only have one target.
  REPLACE_BRANCH(BltOp, BgeOp);
  REPLACE_BRANCH(BeqOp, BneOp);
  REPLACE_BRANCH(BnezOp, BezOp);

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

static std::set<Reg> callerSaved = {
  Reg::t0, Reg::t1, Reg::t2, Reg::t3,
  Reg::t4, Reg::t5, Reg::t6,

  Reg::a0, Reg::a1, Reg::a2, Reg::a3,
  Reg::a4, Reg::a5, Reg::a6, Reg::a7,  
};

static std::set<Reg> calleeSaved = {
  Reg::s0, Reg::s1, Reg::s2, Reg::s3, 
  Reg::s4, Reg::s5, Reg::s6, Reg::s7,
  Reg::s8, Reg::s9, Reg::s10, Reg::s11,
};

void save(Builder builder, const std::vector<Reg> &regs, int offset) {
  for (auto reg : regs) {
    builder.create<sys::rv::StoreOp>({
      /*value=*/new RsAttr(reg),
      /*addr=*/new Rs2Attr(Reg::sp),
      /*offset=*/new IntAttr(offset -= 4),
      /*size=*/new SizeAttr(4)
    });
  }
}

void load(Builder builder, const std::vector<Reg> &regs, int offset) {
  for (auto reg : regs) {
    builder.create<sys::rv::LoadOp>({
      /*value=*/new RdAttr(reg),
      /*addr=*/new RsAttr(Reg::sp),
      /*offset=*/new IntAttr(offset -= 4),
      /*size=*/new SizeAttr(4)
    });
  }
}

void RegAlloc::proEpilogue(FuncOp *funcOp) {
  Builder builder;
  auto usedRegs = usedRegisters[funcOp];
  auto region = funcOp->getRegion();

  // Always preserve return address.
  std::vector<Reg> preserve { Reg::ra };
  for (auto x : usedRegs) {
    if (calleeSaved.count(x))
      preserve.push_back(x);
  }

  // If there's a SubSpOp, then it must be at the top of the first block.
  auto op = region->getFirstBlock()->getFirstOp();
  int offset = 0;
  if (isa<SubSpOp>(op)) {
    offset = op->getAttr<IntAttr>()->value;
    op->removeAttr<IntAttr>();
    op->addAttr<IntAttr>(offset += 4 * preserve.size());
  } else {
    builder.setToRegionStart(region);
    op = builder.create<SubSpOp>({ new IntAttr(offset += 4 * preserve.size()) });
  }

  // Round op to the nearest multiple of 16.
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

  for (auto func : funcs) {
    auto calls = func->findAll<sys::rv::CallOp>();
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
    proEpilogue(func);
    tidyup(func->getRegion());
  }
}
