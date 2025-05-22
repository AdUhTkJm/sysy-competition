#include "ArmPasses.h"

using namespace sys;
using namespace sys::arm;

namespace {

class SpilledRdAttr : public AttrImpl<SpilledRdAttr, ARMLINE + 4194304> {
public:
  bool fp;
  int offset;
  Op *ref;

  SpilledRdAttr(bool fp, int offset, Op *ref): fp(fp), offset(offset), ref(ref) {}

  std::string toString() { return "<rd = " + std::to_string(offset) + (fp ? "f" : "") + ">"; }
  SpilledRdAttr *clone() { return new SpilledRdAttr(fp, offset, ref); }
};

class SpilledRsAttr : public AttrImpl<RegAttr, ARMLINE + 4194304> {
public:
  bool fp;
  int offset;
  Op *ref;

  SpilledRsAttr(bool fp, int offset, Op *ref): fp(fp), offset(offset), ref(ref) {}

  std::string toString() { return "<rs = " + std::to_string(offset) + (fp ? "f" : "") + ">"; }
  SpilledRsAttr *clone() { return new SpilledRsAttr(fp, offset, ref); }
};

class SpilledRs2Attr : public AttrImpl<SpilledRs2Attr, ARMLINE + 4194304> {
public:
  bool fp;
  int offset;
  Op *ref;

  SpilledRs2Attr(bool fp, int offset, Op *ref): fp(fp), offset(offset), ref(ref) {}

  std::string toString() { return "<rs2 = " + std::to_string(offset) + + (fp ? "f" : "") + ">"; }
  SpilledRs2Attr *clone() { return new SpilledRs2Attr(fp, offset, ref); }
};

}

std::map<std::string, int> RegAlloc::stats() {
  return {
    { "spilled", spilled },
    { "peepholed", convertedTotal },
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
#define ADD_ATTR(Index, AttrTy) \
  auto v##Index = op->getOperand(Index).defining; \
  if (!spillOffset.count(v##Index)) \
    op->add<AttrTy>(getReg(v##Index)); \
  else \
    op->add<Spilled##AttrTy>(v##Index->getResultType() == Value::f32, spillOffset[v##Index], v##Index);

#define GET_SPILLED_ARGS(op) \
  (op->getResultType() == Value::f32, spillOffset[op], op)

#define BINARY ADD_ATTR(0, RsAttr) ADD_ATTR(1, Rs2Attr)
#define UNARY ADD_ATTR(0, RsAttr)

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
  builder.create<BOp>({ new TargetAttr(ifnot) })

#define END_REPLACE \
  op->remove<ElseAttr>(); \
  return true

#define CREATE_MV(fp, rd, rs) \
  if (!fp) \
    builder.create<MovROp>({ RDC(rd), RSC(rs) }); \
  else \
    builder.create<FmovOp>({ RDC(rd), RSC(rs) });

// We use dedicated registers as the "spill" register, for simplicity.
static const Reg fargRegs[] = {
  Reg::v0, Reg::v1, Reg::v2, Reg::v3,
  Reg::v4, Reg::v5, Reg::v6, Reg::v7,
};
static const Reg argRegs[] = {
  Reg::x0, Reg::x1, Reg::x2, Reg::x3,
  Reg::x4, Reg::x5, Reg::x6, Reg::x7,
};

static const Reg spillReg = Reg::x28;
static const Reg spillReg2 = Reg::x15;
static const Reg fspillReg = Reg::v31;
static const Reg fspillReg2 = Reg::v15;

// Order for leaf functions. Prioritize temporaries.
static const Reg leafOrder[] = {
  Reg::x0, Reg::x1, Reg::x2, Reg::x3,
  Reg::x4, Reg::x5, Reg::x6, Reg::x7,

  Reg::x8, Reg::x9, Reg::x10, Reg::x11,
  Reg::x12, Reg::x13, Reg::x14,

  Reg::x19, Reg::x20, Reg::x21, Reg::x22,
  Reg::x23, Reg::x24, Reg::x25, Reg::x26,
  Reg::x27,
};
// Order for non-leaf functions.
static const Reg normalOrder[] = {
  Reg::x19, Reg::x20, Reg::x21, Reg::x22,
  Reg::x23, Reg::x24, Reg::x25, Reg::x26,
  Reg::x27,

  Reg::x0, Reg::x1, Reg::x2, Reg::x3,
  Reg::x4, Reg::x5, Reg::x6, Reg::x7,

  Reg::x8, Reg::x9, Reg::x10, Reg::x11,
  Reg::x12, Reg::x13, Reg::x14,
};

// The same, but for floating point registers.
static const Reg leafOrderf[] = {
  Reg::v0, Reg::v1, Reg::v2, Reg::v3,
  Reg::v4, Reg::v5, Reg::v6, Reg::v7,

  Reg::v8, Reg::v9, Reg::v10, Reg::v11,
  Reg::v12, Reg::v13, Reg::v14,

  Reg::v16, Reg::v17, Reg::v18,
  Reg::v19, Reg::v20, Reg::v21, Reg::v22,
  Reg::v23, Reg::v24, Reg::v25, Reg::v26,
  Reg::v27, Reg::v28, Reg::v29, Reg::v30,
};
// Order for non-leaf functions.
static const Reg normalOrderf[] = {
  Reg::v19, Reg::v20, Reg::v21, Reg::v22,
  Reg::v23, Reg::v24, Reg::v25, Reg::v26,
  Reg::v27, Reg::v28, Reg::v29, Reg::v30,

  Reg::v0, Reg::v1, Reg::v2, Reg::v3,
  Reg::v4, Reg::v5, Reg::v6, Reg::v7,

  Reg::v8, Reg::v9, Reg::v10, Reg::v11,
  Reg::v12, Reg::v13, Reg::v14,
};

static const std::set<Reg> callerSaved = {
  Reg::x0, Reg::x1, Reg::x2, Reg::x3,
  Reg::x4, Reg::x5, Reg::x6, Reg::x7,

  Reg::x8, Reg::x9, Reg::x10, Reg::x11,
  Reg::x12, Reg::x13, Reg::x14, Reg::x15,

  Reg::v0, Reg::v1, Reg::v2, Reg::v3,
  Reg::v4, Reg::v5, Reg::v6, Reg::v7,

  Reg::v8, Reg::v9, Reg::v10, Reg::v11,
  Reg::v12, Reg::v13, Reg::v14, Reg::v15,
};

static const std::set<Reg> calleeSaved = {
  Reg::x19, Reg::x20, Reg::x21, Reg::x22,
  Reg::x23, Reg::x24, Reg::x25, Reg::x26,
  Reg::x27, Reg::x28,

  Reg::v16, Reg::v17, Reg::v18,
  Reg::v19, Reg::v20, Reg::v21, Reg::v22,
  Reg::v23, Reg::v24, Reg::v25, Reg::v26,
  Reg::v27, Reg::v28, Reg::v29, Reg::v30,
};

constexpr int leafRegCnt = 24;
constexpr int leafRegCntf = 30;
constexpr int normalRegCnt = 24;
constexpr int normalRegCntf = 30;

static bool isConstant(Op *op) {
  return isa<MovIOp>(op) || isa<AdrOp>(op);
}

// Used in constructing interference graph.
struct Event {
  int timestamp;
  bool start;
  Op *op;
};

void RegAlloc::runImpl(Region *region, bool isLeaf) {
  const Reg *order = isLeaf ? leafOrder : normalOrder;
  const Reg *orderf = isLeaf ? leafOrderf : normalOrderf;
  const int regcount = isLeaf ? leafRegCnt : normalRegCnt;
  const int regcountf = isLeaf ? leafRegCntf : normalRegCntf;

  Builder builder;
  
  std::map<Op*, Reg> assignment;

  auto funcOp = region->getParent();

  // First of all, add 35 precolored placeholders before each call.
  // This denotes that a CallOp clobbers those registers.
  runRewriter(funcOp, [&](CallOp *op) {
    builder.setBeforeOp(op);
    for (auto reg : callerSaved) {
      auto placeholder = builder.create<PlaceHolderOp>();
      assignment[placeholder] = reg;
      // Make floating point respect the placeholders.
      if (isFP(reg))
        placeholder->setResultType(Value::f32);
    }
    return false;
  });

  // Similarly, add placeholders around each GetArg.
  // First create placeholders for a0-a7.
  builder.setToRegionStart(region);
  std::vector<Value> argHolders, fargHolders;
  auto argcnt = funcOp->get<ArgCountAttr>()->count;
  for (int i = 0; i < std::min(argcnt, 8); i++) {
    auto placeholder = builder.create<PlaceHolderOp>();
    assignment[placeholder] = argRegs[i];
    argHolders.push_back(placeholder);

    auto fplaceholder = builder.create<PlaceHolderOp>();
    assignment[fplaceholder] = fargRegs[i];
    fargHolders.push_back(fplaceholder);
  }

  auto rawGets = funcOp->findAll<GetArgOp>();
  // We might find some getArgs missing by DCE, so it's not necessarily consecutive.
  std::vector<Op*> getArgs;
  getArgs.resize(argcnt);
  for (auto x : rawGets)
    getArgs[V(x)] = x;

  int fcnt = 0, cnt = 0;
  for (size_t i = 0; i < getArgs.size(); i++) {
    // A missing argument.
    if (!getArgs[i])
      continue;

    Op *op = getArgs[i];
    auto ty = op->getResultType();

    if (ty == Value::f32 && fcnt < 8) {
      builder.setBeforeOp(op);
      builder.create<PlaceHolderOp>({ fargHolders[fcnt] });
      builder.replace<ReadFRegOp>(op, { new RegAttr(fargRegs[fcnt]) });
      fcnt++;
      continue;
    }
    if (ty != Value::f32 && cnt < 8) {
      builder.setBeforeOp(op);
      builder.create<PlaceHolderOp>({ argHolders[cnt] });
      builder.replace<ReadRegOp>(op, { new RegAttr(argRegs[cnt]) });
      cnt++;
      continue;
    }
    // Spilled to stack; don't do anything.
  }

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
        assignment[op] = REG(op);
        priority[op] = 1;
      }
      if (isa<ReadRegOp>(op))
        priority[op] = 1;
      
      if (isa<PhiOp>(op)) {
        priority[op] = 2;
        for (auto x : op->getOperands()) {
          priority[x.defining] = 1;
          prefer[x.defining] = op;
        }
      }

      // We tend to spill constants, because they're easy to rematerialize.
      if (isConstant(op))
        priority[op] = -1;
    }

    // For all liveOuts, they are last-used at place size().
    // If they aren't defined in this block, then `defined[op]` will be zero, which is intended.
    for (auto op : bb->getLiveOut())
      lastUsed[op] = ops.size();

    // We use event-driven approach to optimize it into O(n log n + E).
    std::vector<Event> events;
    for (auto [op, v] : lastUsed) {
      // Don't push empty live range. It's not handled properly.
      if (defined[op] == v)
        continue;
      
      events.push_back(Event { defined[op], true, op });
      events.push_back(Event { v, false, op });
    }

    // Sort with ascending time (i.e. instruction count).
    std::sort(events.begin(), events.end(), [](Event a, Event b) {
      // For the same timestamp, we first set END events as inactive, then deal with START events.
      return a.timestamp == b.timestamp ? (!a.start && b.start) : a.timestamp < b.timestamp;
    });

    std::set<Op*> active;
    for (const auto& event : events) {
      auto op = event.op;
      // Jumps will never interfere.
      if (isa<BOp>(op))
        continue;

      if (event.start) {
        for (Op* activeOp : active) {
          // FP and int are using different registers.
          if (activeOp->getResultType() == Value::f32 ^ op->getResultType() == Value::f32)
            continue;

          interf[op].insert(activeOp);
          interf[activeOp].insert(op);
        }
        active.insert(op);
      } else
        active.erase(op);
    }
  }

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

  std::unordered_map<Op*, int> spillOffset;
  int currentOffset = STACKOFF(funcOp);
  int highest = 0;

  for (auto op : ops) {
    // Do not allocate colored instructions.
    if (assignment.count(op))
      continue;

    std::set<Reg> bad;
    for (auto v : interf[op]) {
      // In the whole function, `sp` and `zero` are read-only.
      if (assignment.count(v) && assignment[v] != Reg::x30 && assignment[v] != Reg::x31)
        bad.insert(assignment[v]);
    }

    if (prefer.count(op)) {
      auto ref = prefer[op];
      // Try to allocate the same register as `ref`.
      if (assignment.count(ref) && !bad.count(assignment[ref])) {
        assignment[op] = assignment[ref];
        continue;
      }
    }

    // See if there's any preferred registers.
    int preferred = -1;
    for (auto use : op->getUses()) {
      if (isa<WriteRegOp>(use)) {
        auto reg = REG(use);
        if (!bad.count(reg)) {
          preferred = (int) reg;
          break;
        }
      }
    }
    if (isa<ReadRegOp>(op)) {
      auto reg = REG(op);
      if (!bad.count(reg))
        preferred = (int) reg;
    }

    if (preferred != -1) {
      assignment[op] = (Reg) preferred;
      continue;
    }

    auto rcnt = op->getResultType() != Value::f32 ? regcount : regcountf;
    auto rorder = op->getResultType() != Value::f32 ? order : orderf;

    for (int i = 0; i < rcnt; i++) {
      if (!bad.count(rorder[i])) {
        assignment[op] = rorder[i];
        break;
      }
    }

    if (assignment.count(op))
      continue;

    spilled++;
    // Spilled. Try to see all spill offsets of conflicting ops.
    int desired = currentOffset;
    std::set<int> conflict;
    for (auto v : interf[op]) {
      if (!spillOffset.count(v))
        continue;

      conflict.insert(spillOffset[v]);
    }

    // Try find a space.
    while (conflict.count(desired))
      desired += 8;

    spillOffset[op] = desired;

    // Update `highest`, which will indicate the size allocated.
    if (desired > highest)
      highest = desired;
  }

  // Allocate more stack space for it.
  if (spillOffset.size())
    STACKOFF(funcOp) = highest + 8;

  const auto getReg = [&](Op *op) {
    return assignment.count(op) ? assignment[op] :
      op->getResultType() == Value::f32 ? orderf[0] : order[0];
  };

  // Convert all operands to registers.
  LOWER(AddXOp, BINARY);
  LOWER(AddWOp, BINARY);
  LOWER(SubWOp, BINARY);
  LOWER(SubXOp, BINARY);
  LOWER(MulWOp, BINARY);
  LOWER(MulXOp, BINARY);
  LOWER(SdivWOp, BINARY);
  LOWER(SdivXOp, BINARY);
  LOWER(MlaOp, BINARY);
  LOWER(AndOp, BINARY);
  LOWER(OrOp, BINARY);
  LOWER(EorOp, BINARY);
  LOWER(BltOp, BINARY);
  LOWER(BgeOp, BINARY);
  LOWER(StoreOp, BINARY);
  LOWER(AndOp, BINARY);
  LOWER(OrOp, BINARY);
  LOWER(StrWOp, BINARY);
  LOWER(StrXOp, BINARY);
  LOWER(StrFOp, BINARY);
  LOWER(LslOp, BINARY);
  LOWER(LsrOp, BINARY);
  LOWER(AsrWOp, BINARY);
  LOWER(AsrXOp, BINARY);
  LOWER(CmpOp, BINARY);
  LOWER(TstOp, BINARY);
  
  LOWER(LdrWOp, UNARY);
  LOWER(LdrXOp, UNARY);
  LOWER(LdrFOp, UNARY);
  LOWER(AddXIOp, UNARY);
  LOWER(AddWIOp, UNARY);
  LOWER(NegOp, UNARY);
  LOWER(AndIOp, UNARY);
  LOWER(OrIOp, UNARY);
  LOWER(EorIOp, UNARY);
  LOWER(LslIOp, UNARY);
  LOWER(LsrIOp, UNARY);
  LOWER(AsrWIOp, UNARY);
  LOWER(AsrXIOp, UNARY);

  // Note that some ops are dealt with later.
  // We can't remove all operands here.
  for (auto bb : region->getBlocks()) {
    for (auto op : bb->getOps()) {
      if (isa<PlaceHolderOp>(op) || isa<CallOp>(op) || isa<RetOp>(op))
        op->removeAllOperands();
    }
  }

  // Remove placeholders inserted previously.
  auto holders = funcOp->findAll<PlaceHolderOp>();
  for (auto holder : holders)
    holder->erase();

  //   writereg %1, <reg = a0>
  // becomes
  //   mv a0, assignment[%1]
  // As RdAttr is supplied, though `assignment[]` won't have the new op recorded, it's fine.
  runRewriter(funcOp, [&](WriteRegOp *op) {
    builder.setBeforeOp(op);
    CREATE_MV(isFP(REG(op)), REG(op), getReg(op->DEF(0)));
    auto mv = op->prevOp();

    if (spillOffset.count(op->DEF(0))) {
      mv->remove<RsAttr>();
      mv->add<SpilledRsAttr> GET_SPILLED_ARGS(op->DEF(0));
    }

    op->erase();
    return false;
  });

  //   readreg %1, <reg = a0>
  // becomes
  //   mv assignment[%1], a0
  runRewriter(funcOp, [&](ReadRegOp *op) {
    builder.setBeforeOp(op);
    CREATE_MV(isFP(REG(op)), getReg(op), REG(op));
    auto mv = op->prevOp();
    assignment[mv] = getReg(op);

    if (spillOffset.count(op)) {
      mv->remove<RdAttr>();
      mv->add<SpilledRdAttr> GET_SPILLED_ARGS(op);
      spillOffset[mv] = spillOffset[op];
    }

    // We can't directly erase it because it might get used by phi's later.
    op->replaceAllUsesWith(mv);
    op->erase();
    return false;
  });

  // Finally, after everything has been erased:
  // Destruct phi.

  // This contains all phis to be removed.
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
    auto target = bbTerm->get<TargetAttr>();
    auto oldTarget = target->bb;
    target->bb = edge1;

    builder.setToBlockEnd(edge1);
    builder.create<BOp>({ new TargetAttr(oldTarget) });

    // Create edge for else branch.
    auto ifnot = bbTerm->get<ElseAttr>();
    auto oldElse = ifnot->bb;
    ifnot->bb = edge2;

    builder.setToBlockEnd(edge2);
    builder.create<BOp>({ new TargetAttr(oldElse) });

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


#define SPILLABLE(op, Ty) (op->has<Ty##Attr>() ? op->get<Ty##Attr>()->reg : (Reg) -op->get<Spilled##Ty##Attr>()->offset)

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
        auto def = ops[i].defining;
        Op *mv;
        if (phi->getResultType() == Value::f32) {
          mv = builder.create<FmovOp>({
            new ImpureAttr,
            spillOffset.count(phi) ? (Attr*) new SpilledRdAttr GET_SPILLED_ARGS(phi) : new RdAttr(getReg(phi)),
            spillOffset.count(def) ? (Attr*) new SpilledRsAttr GET_SPILLED_ARGS(def) : new RsAttr(getReg(def))
          });
        } else {
          mv = builder.create<MovROp>({
            new ImpureAttr,
            spillOffset.count(phi) ? (Attr*) new SpilledRdAttr GET_SPILLED_ARGS(phi) : new RdAttr(getReg(phi)),
            spillOffset.count(def) ? (Attr*) new SpilledRsAttr GET_SPILLED_ARGS(def) : new RsAttr(getReg(def))
          });
        }
        moves.push_back(mv);
      }
    }

    // Detect circular copies.
    std::map<Reg, Reg> moveMap;
    std::map<std::pair<Reg, Reg>, Op*> moveOpMap;
    std::set<Reg> srcs;
    std::set<Reg> dsts;

    for (auto mv : moves) {
      auto src = SPILLABLE(mv, Rd);
      auto dst = SPILLABLE(mv, Rs);
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
      auto src = mv->has<RdAttr>() ? RD(mv) : (Reg) -mv->get<SpilledRdAttr>()->offset;
      auto dst = mv->has<RsAttr>() ? RS(mv) : (Reg) -mv->get<SpilledRsAttr>()->offset;
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
      // Unhandled yet.
      assert(false);
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

  for (auto phi : allPhis) {
    if (phi->getUses().size())
      module->dump();
    
    phi->erase();
  }

  for (auto bb : region->getBlocks()) {
    for (auto op : bb->getOps()) {
      if (hasRd(op) && !op->has<RdAttr>() && !op->has<SpilledRdAttr>()) {
        if (!spillOffset.count(op))
          op->add<RdAttr>(getReg(op));
        else
          op->add<SpilledRdAttr>(op->getResultType() == Value::f32, spillOffset[op], op);
      }
    }
  }

  // Deal with spilled variables.
  std::vector<Op*> rematerialized;
  for (auto bb : region->getBlocks()) {
    int delta = 0;
    for (auto op : bb->getOps()) {
      // We might encounter spilling around calls.
      // For example:
      //   addi sp, sp, -192    ; setting up 24 extra arguments
      //   mv a0, ...
      //   ld s11, OFFSET(sp)   ; !!! ADJUST HERE
      //
      // That's why we need an extra "delta".
      // No need for dominance analysis etc. because the SubSp is well-bracketed inside a block.
      if (isa<SubSpOp>(op)) {
        delta += V(op);
        continue;
      }

      if (auto rd = op->find<SpilledRdAttr>()) {
        int offset = delta + rd->offset;
        bool fp = rd->fp;
        auto reg = fp ? fspillReg : spillReg;
        if (isConstant(rd->ref)) {
          // Mark for removal and rematerialize when needed.
          rematerialized.push_back(rd->ref);
          continue;
        }

        builder.setAfterOp(op);
        if (offset < 2048)
          builder.create<StoreOp>({ RSC(reg), RS2C(Reg::x31), new IntAttr(offset), new SizeAttr(8) });
        else if (offset < 4096) {
          builder.create<AddWIOp>({ RDC(spillReg2), RSC(Reg::x31), new IntAttr(2047), new SizeAttr(8) });
          builder.create<StoreOp>({ RSC(reg), RS2C(spillReg2), new IntAttr(offset - 2047), new SizeAttr(8) });
        }
        else assert(false);
        op->add<RdAttr>(reg);
      }

      if (auto rs = op->find<SpilledRsAttr>()) {
        int offset = delta + rs->offset;
        bool fp = rs->fp;
        auto reg = fp ? fspillReg : spillReg;
        auto ldty = fp ? Value::f32 : Value::i64;

        builder.setBeforeOp(op);
        if (isConstant(rs->ref)) {
          auto ref = rs->ref;
          // Copy it here.
          if (isa<MovIOp>(ref))
            builder.create<MovIOp>({ RDC(reg), new IntAttr(V(ref)) });
          if (isa<AdrOp>(ref))
            builder.create<AdrOp>({ RDC(reg), new NameAttr(NAME(ref)) });
        } else if (offset < 2048)
          builder.create<LoadOp>(ldty, { RDC(reg), RSC(Reg::x31), new IntAttr(offset), new SizeAttr(8) });
        else if (offset < 4096) {
          builder.create<AddWIOp>({ RDC(spillReg), RSC(Reg::x31), new IntAttr(2047), new SizeAttr(8) });
          builder.create<LoadOp>(ldty, { RDC(reg), RSC(spillReg), new IntAttr(offset - 2047), new SizeAttr(8) });
        }
        else assert(false);
        op->add<RsAttr>(reg);
      }

      if (auto rs2 = op->find<SpilledRs2Attr>()) {
        int offset = delta + rs2->offset;
        bool fp = rs2->fp;
        auto reg = fp ? fspillReg2 : spillReg2;
        auto ldty = fp ? Value::f32 : Value::i64;

        builder.setBeforeOp(op);
        if (isConstant(rs2->ref)) {
          auto ref = rs2->ref;
          // Copy it here.
          if (isa<MovIOp>(ref))
            builder.create<MovIOp>({ RDC(reg), new IntAttr(V(ref)) });
          if (isa<AdrOp>(ref))
            builder.create<AdrOp>({ RDC(reg), new NameAttr(NAME(ref)) });
        } else if (offset < 2048)
          builder.create<LoadOp>(ldty, { RDC(reg), RSC(Reg::x31), new IntAttr(offset), new SizeAttr(8) });
        else if (offset < 4096) {
          builder.create<AddWIOp>({ RDC(spillReg2), RSC(Reg::x31), new IntAttr(2047), new SizeAttr(8) });
          builder.create<LoadOp>(ldty, { RDC(reg), RSC(spillReg2), new IntAttr(offset - 2047), new SizeAttr(8) });
        }
        else assert(false);
        op->add<Rs2Attr>(reg);
      }
    }
  }

  for (auto x : rematerialized)
    x->erase();
}

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
  std::vector<StoreOp*> stores;
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
          RS(op) == Reg::x30 && RS2(op) == Reg::x31 &&
          RS(next) == Reg::x30 && RS2(next) == Reg::x31 &&
          V(op) % 8 == 0 && SIZE(op) == 4 &&
          V(next) == V(op) + 4 && SIZE(next) == 4) {
        converted++;
        changed = true;

        auto offset = V(op);
        builder.replace<StoreOp>(op, {
          new RsAttr(Reg::x30),
          new Rs2Attr(Reg::x31),
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
          RS(op) == Reg::x30 && RS2(op) == Reg::x31 &&
          RS(next) == Reg::x30 && RS2(next) == Reg::x31 &&
          V(op) % 8 == 4 && SIZE(op) == 4 &&
          V(next) == V(op) - 4 && SIZE(next) == 4) {
        converted++;
        changed = true;

        auto offset = V(op);
        builder.replace<StoreOp>(op, {
          new RsAttr(Reg::x30),
          new Rs2Attr(Reg::x31),
          new IntAttr(offset - 4),
          new SizeAttr(8),
        });
        next->erase();
        break;
      }
    }
  } while (changed);

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
    if (bb->getOps().size() == 1 && isa<BOp>(bb->getLastOp())) {
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
  REPLACE_BRANCH(BltOp, BgeOp);
  REPLACE_BRANCH(BeqOp, BneOp);

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
  for (auto reg : regs) {
    offset -= 8;
    if (offset < 2048) {
      builder.create<StrXOp>({
        new RsAttr(reg),
        new Rs2Attr(Reg::x31),
        new IntAttr(offset)
      });
    } else {
      // li t6, offset
      // addi t6, t6, sp
      // sd reg, 0(t6)
      // (Because reg might be `s11`)
      builder.create<MovIOp>({
        new RdAttr(spillReg2),
        new IntAttr(offset)
      });
      builder.create<AddXOp>({
        new RdAttr(spillReg2),
        new RsAttr(spillReg2),
        new Rs2Attr(Reg::x31)
      });
      builder.create<StrXOp>({
        new RsAttr(reg),
        new Rs2Attr(spillReg2),
        new IntAttr(0)
      });
    }
  }
}

void load(Builder builder, const std::vector<Reg> &regs, int offset) {
  for (auto reg : regs) {
    offset -= 8;
    auto isFloat = isFP(reg);
    Value::Type ty = isFloat ? Value::f32 : Value::i64;
    if (offset < 2048) {
      builder.create<LdrXOp>({
        new RdAttr(reg),
        new RsAttr(Reg::x31),
        new IntAttr(offset),
      });
    } else {
      // li s11, offset
      // addi s11, s11, sp
      // ld reg, 0(s11)
      builder.create<MovIOp>({
        new RdAttr(spillReg),
        new IntAttr(offset)
      });
      builder.create<AddXOp>({
        new RdAttr(spillReg),
        new RsAttr(spillReg),
        new Rs2Attr(Reg::x31)
      });
      builder.create<LdrXOp>({
        new RdAttr(reg),
        new RsAttr(spillReg),
        new IntAttr(0)
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
    builder.replace<LoadOp>(op, isFP(RD(op)) ? Value::f32 : Value::i64, {
      new RdAttr(RD(op)),
      new RsAttr(Reg::x31),
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
    if (offset <= 2048 && offset > -2048) {
      builder.replace<AddXIOp>(op, {
        new RdAttr(Reg::x31),
        new RsAttr(Reg::x31),
        new IntAttr(-offset)
      });
    } else {
      builder.setBeforeOp(op);
      builder.create<MovIOp>({
        new RdAttr(spillReg2),
        new IntAttr(offset)
      });
      builder.replace<SubXOp>(op, {
        new RdAttr(Reg::x31),
        new RsAttr(Reg::x31),
        new Rs2Attr(spillReg2)
      });
    }
    return true;
  });
}

void RegAlloc::run() {
  auto funcs = collectFuncs();
  fnMap = getFunctionMap();
  std::set<FuncOp*> leaves;

  for (auto func : funcs) {
    auto calls = func->findAll<BrOp>();
    if (calls.size() == 0)
      leaves.insert(func);
    runImpl(func->getRegion(), calls.size() == 0);
  }

  // Have a look at what registers are used inside each function.
  for (auto func : funcs) {
    auto &set = usedRegisters[func];
    for (auto bb : func->getRegion()->getBlocks()) {
      for (auto op : bb->getOps()) {
        if (op->has<RdAttr>())
          set.insert(op->get<RdAttr>()->reg);
        if (op->has<RsAttr>())
          set.insert(op->get<RsAttr>()->reg);
        if (op->has<Rs2Attr>())
          set.insert(op->get<Rs2Attr>()->reg);
      }
    }
  }

  for (auto func : funcs) {
    proEpilogue(func, leaves.count(func));
    tidyup(func->getRegion());
  }
}
