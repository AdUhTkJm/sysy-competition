#include "RvPasses.h"
#include "Regs.h"

using namespace sys::rv;
using namespace sys;

#define CREATE_MV(fp, rd, rs) \
  if (!fp) \
    builder.create<MvOp>({ RDC(rd), RSC(rs) }); \
  else \
    builder.create<FmvOp>({ RDC(rd), RSC(rs) });

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
        op->get<RsAttr>(), \
        op->get<Rs2Attr>(), \
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
  op->remove<ElseAttr>(); \
  return true
int RegAlloc::latePeephole(Op *funcOp) {
  Builder builder;

  int converted = 0;

  runRewriter(funcOp, [&](StoreOp *op) {
    if (op == op->getParent()->getLastOp())
      return false;

    //   sw a0, N(addr)
    //   lw a1, N(addr)
    // becomes
    //   sw a0, N(addr)
    //   mv a1, a0
    auto next = op->nextOp();
    if (isa<LoadOp>(next) &&
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
    stores = funcOp->findAll<StoreOp>();
    for (auto op : stores) {
      if (op == op->getParent()->getLastOp())
        continue;
      auto next = op->nextOp();

      //   sw zero, N(sp)
      //   sw zero, N+4(sp)
      // becomes
      //   sd zero, N(sp)
      // only when N is a multiple of 8.
      //
      // We know `sp` is 16-aligned, but we don't know for other registers.
      // That's why we fold it only for `sp`.
      if (isa<StoreOp>(next) &&
          RS(op) == Reg::zero && RS2(op) == Reg::sp &&
          RS(next) == Reg::zero && RS2(next) == Reg::sp &&
          V(op) % 8 == 0 && SIZE(op) == 4 &&
          V(next) == V(op) + 4 && SIZE(next) == 4) {
        converted++;
        changed = true;

        auto offset = V(op);
        builder.replace<StoreOp>(op, {
          RSC(Reg::zero),
          RS2C(Reg::sp),
          new IntAttr(offset),
          new SizeAttr(8),
        });
        next->erase();
        break;
      }

      // Similarly:
      //   sw zero, N(sp)
      //   sw zero, N-4(sp)
      // becomes
      //   sd zero, N-4(sp)
      // only when N-4 is a multiple of 8.
      if (isa<StoreOp>(next) &&
          RS(op) == Reg::zero && RS2(op) == Reg::sp &&
          RS(next) == Reg::zero && RS2(next) == Reg::sp &&
          V(op) % 8 == 4 && SIZE(op) == 4 &&
          V(next) == V(op) - 4 && SIZE(next) == 4) {
        converted++;
        changed = true;

        auto offset = V(op);
        builder.replace<StoreOp>(op, {
          RSC(Reg::zero),
          RS2C(Reg::sp),
          new IntAttr(offset - 4),
          new SizeAttr(8),
        });
        next->erase();
        break;
      }
    }
  } while (changed);

  // Eliminate useless MvOp.
  runRewriter(funcOp, [&](MvOp *op) {
    if (RD(op) == RS(op)) {
      converted++;
      op->erase();
      return true;
    }

    // mv  a0, a1    <-- op
    // mv  a1, a0    <-- mv2
    // We can delete the second operation, `mv2`.
    if (op != op->getParent()->getLastOp()) {
      auto mv2 = op->nextOp();
      if (isa<MvOp>(mv2) && RD(op) == RS(mv2) && RS(op) == RD(mv2)) {
        // We can't erase `mv2` because it might be explored afterwards.
        // We need to change the content of mv2 and erase this one.
        op->erase();
        std::swap(RD(mv2), RS(mv2));
      }
    }
    return false;
  });

  runRewriter(funcOp, [&](FmvOp *op) {
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

  // Replace blocks with only a single `j` as terminator.
  std::map<BasicBlock*, BasicBlock*> jumpTo;
  for (auto bb : region->getBlocks()) {
    if (bb->getOps().size() == 1 && isa<JOp>(bb->getLastOp())) {
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
  
  // After lowering, combine sequential blocks into one.
  // Simpler than simplify-cfg, because no phis could remain now.
  do {
    changed = false;
    const auto &bbs = region->getBlocks();
    for (auto bb : bbs) {
      if (bb->succs.size() != 1)
        continue;

      auto succ = *bb->succs.begin();
      if (succ->preds.size() != 1)
        continue;

      // Remove the jump to `succ`.
      auto term = bb->getLastOp();
      if (isa<JOp>(term))
        term->erase();
      
      // All successors of `succ` now have pred `bb`.
      // `bb` also regard them as successors.
      for (auto s : succ->succs) {
        s->preds.erase(succ);
        s->preds.insert(bb);
        bb->succs.insert(s);
      }
      // Remove `succ` from the successors of `bb`.
      bb->succs.erase(succ);

      // Then move all instruction in `succ` to `bb`.
      auto ops = succ->getOps();
      for (auto op : ops)
        op->moveToEnd(bb);

      succ->forceErase();
      changed = true;
      break;
    }
  } while (changed);

  // Now branches are still having both TargetAttr and ElseAttr.
  // Replace them (perform split when necessary), so that they only have one target.
  REPLACE_BRANCH(BltOp, BgeOp);
  REPLACE_BRANCH(BeqOp, BneOp);
  REPLACE_BRANCH(BleOp, BgtOp);

  int converted;
  do {
    converted = latePeephole(funcOp);
    convertedTotal += converted;
  } while (converted);

  // Also, eliminate useless JOp.
  runRewriter(funcOp, [&](JOp *op) {
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
  using sys::rv::StoreOp;

  for (auto reg : regs) {
    offset -= 8;
    if (offset < 2048)
      builder.create<StoreOp>({ RSC(reg), RS2C(Reg::sp), new IntAttr(offset), new SizeAttr(8) });
    else {
      // li   t6, offset
      // addi t6, t6, sp
      // sd   reg, 0(t6)
      // (Because reg might be `s11`)
      builder.create<LiOp>({ RDC(spillReg2), new IntAttr(offset) });
      builder.create<AddOp>({ RDC(spillReg2), RSC(spillReg2), RS2C(Reg::sp) });
      builder.create<StoreOp>({ RSC(reg), RS2C(spillReg2), new IntAttr(0), new SizeAttr(8) });
    }
  }
}

void load(Builder builder, const std::vector<Reg> &regs, int offset) {
  using sys::rv::LoadOp;

  for (auto reg : regs) {
    offset -= 8;
    auto isFloat = isFP(reg);
    Value::Type ty = isFloat ? Value::f32 : Value::i64;
    if (offset < 2048)
      builder.create<LoadOp>(ty, { RDC(reg), RSC(Reg::sp), new IntAttr(offset), new SizeAttr(8) });
    else {
      // li   s11, offset
      // addi s11, s11, sp
      // ld   reg, 0(s11)
      builder.create<LiOp>({ RDC(spillReg), new IntAttr(offset) });
      builder.create<AddOp>({ RDC(spillReg), RSC(spillReg), RS2C(Reg::sp) });
      builder.create<LoadOp>(ty, { RDC(reg), RSC(spillReg), new IntAttr(0), new SizeAttr(8) });
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
      builder.replace<JOp>(ret, { new TargetAttr(bb) });

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
    builder.replace<LoadOp>(op, isFP(RD(op)) ? Value::f32 : Value::i64, {
      RDC(RD(op)),
      RSC(Reg::sp),
      new IntAttr(myoffset),
      new SizeAttr(8)
    });
    return false;
  });

  //   subsp <4>
  // becomes
  //   addi <rd = sp> <rs = sp> <-4>
  runRewriter(funcOp, [&](SubSpOp *op) {
    int offset = V(op);
    if (offset <= 2048 && offset > -2048)
      builder.replace<AddiOp>(op, { RDC(Reg::sp), RSC(Reg::sp), new IntAttr(-offset) });
    else {
      builder.setBeforeOp(op);
      builder.create<LiOp>({ RDC(Reg::t0), new IntAttr(offset) });
      builder.replace<SubOp>(op, {
        RDC(Reg::sp),
        RSC(Reg::sp),
        RS2C(Reg::t0)
      });
    }
    return true;
  });
}
