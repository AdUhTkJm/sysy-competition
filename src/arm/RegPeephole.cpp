#include "ArmPasses.h"
#include "Regs.h"

using namespace sys::arm;
using namespace sys;


#define REPLACE_BRANCH(T1, T2, ...) \
  REPLACE_BRANCH_IMPL(T1, T2, __VA_ARGS__); \
  REPLACE_BRANCH_IMPL(T2, T1, __VA_ARGS__)

// Say the before is `blt`, then we might see
//   blt %1 %2 <target = bb1> <else = bb2>
// which means `if (%1 < %2) goto bb1 else goto bb2`.
//
// If the next block is just <bb1>, then we flip it to bge, and make the target <bb2>.
// if the next block is <bb2>, then we make the target <bb2>.
// otherwise, make the target <bb1>, and add another `j <bb2>`.
#define REPLACE_BRANCH_IMPL(BeforeTy, AfterTy, ...) \
  runRewriter(funcOp, [&](BeforeTy *op) { \
    if (!op->has<ElseAttr>()) \
      return false; \
    auto &target = TARGET(op); \
    auto ifnot = ELSE(op); \
    auto me = op->getParent(); \
    /* If there's no "next block", then give up */ \
    if (me == me->getParent()->getLastBlock()) { \
      GENERATE_J; \
      END_REPLACE; \
    } \
    if (me->nextBlock() == target) { \
      builder.replace<AfterTy>(op, { \
        new TargetAttr(ifnot), \
        __VA_ARGS__ \
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
  builder.create<BOp>({ new TargetAttr(ifnot) })

#define END_REPLACE \
  op->remove<ElseAttr>(); \
  return true

#define UNARY_BRANCH RSC(RS(op))
#define BINARY_BRANCH RSC(RS(op)), RS2C(RS2(op))

#define CREATE_MV(fp, rd, rs) \
  if (!fp) \
    builder.create<MovROp>({ RDC(rd), RSC(rs) }); \
  else \
    builder.create<FmovOp>({ RDC(rd), RSC(rs) });

int RegAlloc::latePeephole(Op *funcOp) {
  Builder builder;

  int converted = 0;

  runRewriter(funcOp, [&](StrWOp *op) {
    if (op == op->getParent()->getLastOp())
      return false;

    //   sw a0, N(addr)
    //   lw a1, N(addr)
    // becomes
    //   sw a0, N(addr)
    //   mv a1, a0
    auto next = op->nextOp();
    if (isa<LdrWOp>(next) &&
        RS(next) == RS2(op) && V(next) == V(op) && SIZE(next) == SIZE(op)) {
      converted++;
      builder.setBeforeOp(next);
      CREATE_MV(isFP(RD(next)), RD(next), RS(op));
      next->erase();
      return true;
    }

    return false;
  });

  bool changed;
  std::vector<Op*> stores;
  do {
    changed = false;
    // This modifies the content of stores, so cannot run in a rewriter.
    stores = funcOp->findAll<StrWOp>();
    for (auto op : stores) {
      if (op == op->getParent()->getLastOp())
        continue;
      auto next = op->nextOp();

      //   str wzr, N(sp)
      //   str wzr, N+4(sp)
      // becomes
      //   str xzr, N(sp)
      // only when N is a multiple of 8.
      //
      // We know `sp` is 16-aligned, but we don't know for other registers.
      // That's why we fold it only for `sp`.
      if (isa<StrWOp>(next) &&
          RS(op) == Reg::xzr && RS2(op) == Reg::sp &&
          RS(next) == Reg::xzr && RS2(next) == Reg::sp &&
          V(op) % 8 == 0 && V(next) == V(op) + 4) {
        converted++;
        changed = true;

        auto offset = V(op);
        builder.replace<StrXOp>(op, {
          RSC(Reg::xzr),
          RS2C(Reg::sp),
          new IntAttr(offset),
        });
        next->erase();
        break;
      }
      
      if (isa<StrWOp>(next) &&
          RS(op) == Reg::xzr && RS2(op) == Reg::sp &&
          RS(next) == Reg::xzr && RS2(next) == Reg::sp &&
          V(op) % 8 == 4 && V(next) == V(op) - 4) {
        converted++;
        changed = true;

        auto offset = V(op);
        builder.replace<StrWOp>(op, {
          RSC(Reg::xzr),
          RS2C(Reg::sp),
          new IntAttr(offset - 4),
        });
        next->erase();
        break;
      }
    }
  } while (changed);

  runRewriter(funcOp, [&](MulVOp *op) {
    if (op == op->getParent()->getLastOp())
      return false;

    //   mul v0, v1, v2
    //   add v3, v3, v0 (or in the opposite order)
    // becomes 
    //   mla v3, v1, v2
    // This isn't doable in inst-select because it modifies v3 inplace.
    auto next = op->nextOp();
    if (isa<AddVOp>(next) &&
       (RS(next) == RD(next) && RS2(next) == RD(op)
     || RS2(next) == RD(next) && RS(next) == RD(op))) {
      converted++;
      builder.setBeforeOp(next);
      builder.replace<MlaVOp>(next, { RDC(RD(next)), RSC(RS(op)), RS2C(RS2(op)) });
      op->erase();
    }

    return false;
  });

  // Eliminate useless MovROp.
  runRewriter(funcOp, [&](MovROp *op) {
    if (RD(op) == RS(op)) {
      converted++;
      op->erase();
      return true;
    }
    return false;
  });

  runRewriter(funcOp, [&](FmovOp *op) {
    if (RD(op) == RS(op)) {
      converted++;
      op->erase();
      return true;
    }
    return false;
  });

  return converted;
}

void RegAlloc::tidyup(Region *region) {
  Builder builder;
  auto funcOp = region->getParent();
  region->updatePreds();

  int converted;
  do {
    converted = latePeephole(funcOp);
    convertedTotal += converted;
  } while (converted);

  // Replace blocks with only a single `j` as terminator.
  std::map<BasicBlock*, BasicBlock*> jumpTo;
  for (auto bb : region->getBlocks()) {
    if (bb->getOpCount() == 1 && isa<BOp>(bb->getLastOp())) {
      auto target = bb->getLastOp()->get<TargetAttr>()->bb;
      jumpTo[bb] = target;
    }
  }

  // Calculate jump-to closure.
  bool changed;
  do {
    changed = false;
    for (auto [k, v] : jumpTo) {
      if (jumpTo.count(v)) {
        jumpTo[k] = jumpTo[v];
        changed = true;
      }
    }
  } while (changed);

  for (auto bb : region->getBlocks()) { 
    auto term = bb->getLastOp();
    if (auto target = term->find<TargetAttr>()) {
      if (jumpTo.count(target->bb))
        target->bb = jumpTo[target->bb];
    }

    if (auto ifnot = term->find<ElseAttr>()) {
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
  REPLACE_BRANCH(BltOp, BgeOp, BINARY_BRANCH);
  REPLACE_BRANCH(BleOp, BgtOp, BINARY_BRANCH);
  REPLACE_BRANCH(BeqOp, BneOp, BINARY_BRANCH);
  REPLACE_BRANCH(CbzOp, CbnzOp, UNARY_BRANCH);

  // Also, eliminate useless BOp.
  runRewriter(funcOp, [&](BOp *op) {
    auto &target = TARGET(op);
    auto me = op->getParent();
    if (me == me->getParent()->getLastBlock())
      return false;

    if (me->nextBlock() == target) {
      op->erase();
      return true;
    }
    return false;
  });
}

void save(Builder builder, const std::vector<Reg> &regs, int offset) {
  if (offset >= 512) {
    for (auto reg : regs) {
      offset -= 8;
      bool fp = isFP(reg);
      if (fp)
        builder.create<StrDOp>({ RSC(reg), RS2C(Reg::sp), new IntAttr(offset) });
      else
        builder.create<StrXOp>({ RSC(reg), RS2C(Reg::sp), new IntAttr(offset) });
    }
    return;
  }

  for (int i = 0; i < regs.size() / 2 * 2; i += 2) {
    Reg r1 = regs[i];
    Reg r2 = regs[i + 1]; 
    offset -= 16;
    bool fp = isFP(r1), fp2 = isFP(r2);
    if (fp && fp2)
      builder.create<StpDOp>({ RSC(r1), RS2C(r2), RS3C(Reg::sp), new IntAttr(offset) });
    if (!fp && !fp2)
      builder.create<StpXOp>({ RSC(r1), RS2C(r2), RS3C(Reg::sp), new IntAttr(offset) });
    if (fp && !fp2) {
      builder.create<StrDOp>({ RSC(r1), RS2C(Reg::sp), new IntAttr(offset + 8) });
      builder.create<StrXOp>({ RSC(r2), RS2C(Reg::sp), new IntAttr(offset) });
    }
    if (!fp && fp2) {
      builder.create<StrXOp>({ RSC(r1), RS2C(Reg::sp), new IntAttr(offset + 8) });
      builder.create<StrDOp>({ RSC(r2), RS2C(Reg::sp), new IntAttr(offset) });
    }
  }
  if (regs.size() & 1) {
    Reg reg = regs.back();
    offset -= 8;
    bool fp = isFP(reg);
    if (fp)
      builder.create<StrDOp>({ RSC(reg), RS2C(Reg::sp), new IntAttr(offset) });
    else
      builder.create<StrXOp>({ RSC(reg), RS2C(Reg::sp), new IntAttr(offset) });
  }
}

void load(Builder builder, const std::vector<Reg> &regs, int offset) {
  if (offset >= 512) {
    for (auto reg : regs) {
      offset -= 8;
      bool fp = isFP(reg);
      if (fp)
        builder.create<LdrDOp>({ RDC(reg), RSC(Reg::sp), new IntAttr(offset) });
      else
        builder.create<LdrXOp>({ RDC(reg), RSC(Reg::sp), new IntAttr(offset) });
    }
    return;
  }

  // We don't have two rds, so we'd just use rs and rs2.
  // It won't be used anywhere else anyway.
  for (int i = 0; i < regs.size() / 2 * 2; i += 2) {
    Reg r1 = regs[i];
    Reg r2 = regs[i + 1]; 
    offset -= 16;
    bool fp = isFP(r1), fp2 = isFP(r2);
    if (fp && fp2)
      builder.create<LdpDOp>({ RSC(r1), RS2C(r2), RS3C(Reg::sp), new IntAttr(offset) });
    if (!fp && !fp2)
      builder.create<LdpXOp>({ RSC(r1), RS2C(r2), RS3C(Reg::sp), new IntAttr(offset) });
    if (fp && !fp2) {
      builder.create<LdrDOp>({ RDC(r1), RSC(Reg::sp), new IntAttr(offset + 8) });
      builder.create<LdrXOp>({ RDC(r2), RSC(Reg::sp), new IntAttr(offset) });
    }
    if (!fp && fp2) {
      builder.create<LdrXOp>({ RDC(r1), RSC(Reg::sp), new IntAttr(offset + 8) });
      builder.create<LdrDOp>({ RDC(r2), RSC(Reg::sp), new IntAttr(offset) });
    }
  }
  if (regs.size() & 1) {
    Reg reg = regs.back();
    offset -= 8;
    bool fp = isFP(reg);
    if (fp)
      builder.create<LdrDOp>({ RDC(reg), RSC(Reg::sp), new IntAttr(offset) });
    else
      builder.create<LdrXOp>({ RDC(reg), RSC(Reg::sp), new IntAttr(offset) });
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
    preserve.push_back(Reg::x30);

  // If there's a SubSpOp, then it must be at the top of the first block.
  int &offset = STACKOFF(funcOp);
  offset += 8 * preserve.size();

  // Round op to the nearest multiple of 16.
  // This won't be entered in the special case where offset == 0.
  if (offset % 16 != 0)
    offset = offset / 16 * 16 + 16;

  // Add function prologue, preserving the regs.
  auto entry = region->getFirstBlock();
  builder.setToBlockStart(entry);
  if (offset != 0)
    builder.create<SubSpOp>({ new IntAttr(offset) });
  
  save(builder, preserve, offset);

  // Similarly add function epilogue.
  if (offset != 0) {
    auto rets = funcOp->findAll<RetOp>();
    auto bb = region->appendBlock();
    for (auto ret : rets)
      builder.replace<BOp>(ret, { new TargetAttr(bb) });

    builder.setToBlockStart(bb);

    load(builder, preserve, offset);
    builder.create<SubSpOp>({ new IntAttr(-offset) });
    builder.create<RetOp>();
  }

  // Caller preserved registers are marked correctly as interfered,
  // because of the placeholders.

  // Deal with remaining GetArg.
  // The arguments passed by registers have already been eliminated.
  // Now all remaining ones are passed on stack; sort them according to index.
  auto remainingGets = funcOp->findAll<GetArgOp>();
  std::sort(remainingGets.begin(), remainingGets.end(), [](Op *a, Op *b) {
    return V(a) < V(b);
  });
  std::map<Op*, int> argOffsets;
  int argOffset = 0;

  for (auto op : remainingGets) {
    argOffsets[op] = argOffset;
    argOffset += 8;
  }

  runRewriter(funcOp, [&](GetArgOp *op) {
    auto value = V(op);
    assert(value >= 8);

    // `sp + offset` is the base pointer.
    // We read past the base pointer (starting from 0):
    //    <arg 8> bp + 0
    //    <arg 9> bp + 8
    // ...
    assert(argOffsets.count(op));
    int myoffset = offset + argOffsets[op];
    builder.setBeforeOp(op);
    bool fp = isFP(RD(op));

    if (fp)
      builder.replace<LdrFOp>(op, { RDC(RD(op)), RSC(Reg::sp), new IntAttr(myoffset) });
    else
      builder.replace<LdrXOp>(op, { RDC(RD(op)), RSC(Reg::sp), new IntAttr(myoffset) });
    return false;
  });

  //   subsp <4>
  // becomes
  //   addi <rd = sp> <rs = sp> <-4>
  runRewriter(funcOp, [&](SubSpOp *op) {
    int offset = V(op);
    builder.replace<AddXIOp>(op, { RDC(Reg::sp), RSC(Reg::sp), new IntAttr(-offset) });
    return true;
  });
}