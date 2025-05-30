#include "RvPasses.h"
#include "Regs.h"
#include <unordered_set>

using namespace sys;
using namespace sys::rv;

namespace {

class SpilledRdAttr : public AttrImpl<SpilledRdAttr, RVLINE + 2097152> {
public:
  bool fp;
  int offset;
  Op *ref;

  SpilledRdAttr(bool fp, int offset, Op *ref): fp(fp), offset(offset), ref(ref) {}

  std::string toString() override { return "<rd-spilled = " + std::to_string(offset) + (fp ? "f" : "") + ">"; }
  SpilledRdAttr *clone() override { return new SpilledRdAttr(fp, offset, ref); }
};

class SpilledRsAttr : public AttrImpl<SpilledRsAttr, RVLINE + 2097152> {
public:
  bool fp;
  int offset;
  Op *ref;

  SpilledRsAttr(bool fp, int offset, Op *ref): fp(fp), offset(offset), ref(ref) {}

  std::string toString() override { return "<rs-spilled = " + std::to_string(offset) + (fp ? "f" : "") + ">"; }
  SpilledRsAttr *clone() override { return new SpilledRsAttr(fp, offset, ref); }
};

class SpilledRs2Attr : public AttrImpl<SpilledRs2Attr, RVLINE + 2097152> {
public:
  bool fp;
  int offset;
  Op *ref;

  SpilledRs2Attr(bool fp, int offset, Op *ref): fp(fp), offset(offset), ref(ref) {}

  std::string toString() override { return "<rs2-spilled = " + std::to_string(offset) + + (fp ? "f" : "") + ">"; }
  SpilledRs2Attr *clone() override { return new SpilledRs2Attr(fp, offset, ref); }
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
    op->add<Spilled##AttrTy> GET_SPILLED_ARGS(v##Index);

#define GET_SPILLED_ARGS(op) \
  (op->getResultType() == Value::f32, spillOffset[op], op)

#define BINARY ADD_ATTR(0, RsAttr) ADD_ATTR(1, Rs2Attr)
#define UNARY ADD_ATTR(0, RsAttr)

#define CREATE_MV(fp, rd, rs) \
  if (!fp) \
    builder.create<MvOp>({ RDC(rd), RSC(rs) }); \
  else \
    builder.create<FmvOp>({ RDC(rd), RSC(rs) });

// Implemented in OpBase.cpp.
std::string getValueNumber(Value value);

// For debug purposes
void dumpInterf(Region *region, const std::unordered_map<Op*, std::set<Op*>> &interf) {
  region->dump(std::cerr, /*depth=*/1);
  std::cerr << "\n\n===== interference graph =====\n\n";
  for (auto [k, v] : interf) {
    std::cerr << getValueNumber(k->getResult()) << ": ";
    for (auto op : v)
      std::cerr << getValueNumber(op->getResult()) << " ";
    std::cerr << "\n";
  }
}

void dumpAssignment(Region *region, const std::unordered_map<Op*, Reg> &assignment) {
  region->dump(std::cerr, /*depth=*/1);
  std::cerr << "\n\n===== assignment =====\n\n";
  for (auto [k, v] : assignment) {
    std::cerr << getValueNumber(k->getResult()) << " = " << showReg(v) << "\n";
  }
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
  BasicBlock *entry = region->getFirstBlock();
  for (size_t i = 0; i < getArgs.size(); i++) {
    // A missing argument.
    if (!getArgs[i])
      continue;

    Op *op = getArgs[i];
    auto ty = op->getResultType();

    // It is necessary to put those GetArgs to the front.
    if (ty == Value::f32 && fcnt < 8) {
      op->moveToStart(entry);
      builder.setBeforeOp(op);
      builder.create<PlaceHolderOp>({ fargHolders[fcnt] });
      builder.replace<ReadRegOp>(op, Value::f32, { new RegAttr(fargRegs[fcnt]) });
      fcnt++;
      continue;
    }
    if (ty != Value::f32 && cnt < 8) {
      op->moveToStart(entry);
      builder.setBeforeOp(op);
      builder.create<PlaceHolderOp>({ argHolders[cnt] });
      builder.replace<ReadRegOp>(op, Value::i32, { new RegAttr(argRegs[cnt]) });
      cnt++;
      continue;
    }
    // Spilled to stack; don't do anything.
  }

  region->updateLiveness();

  // Interference graph.
  std::unordered_map<Op*, std::set<Op*>> interf, spillInterf;

  // Values of readreg, or operands of writereg, or phis (mvs), are prioritzed.
  std::unordered_map<Op*, int> priority;
  // The `key` is preferred to have the same value as `value`.
  std::unordered_map<Op*, Op*> prefer;
  // Maps a phi to its operands.
  std::unordered_map<Op*, std::vector<Op*>> phiOperand;

  int currentPriority = 2;
  for (auto bb : region->getBlocks()) {
    // Scan through the block and see the place where the value's last used.
    std::unordered_map<Op*, int> lastUsed, defined;
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
      
      if (isa<LiOp>(op)) {
        if (V(op) <= 2047 && V(op) >= -2048)
          priority[op] = -2;
        priority[op] = -1;
      }
      if (isa<LaOp>(op))
        priority[op] = -1;

      if (isa<PhiOp>(op)) {
        priority[op] = currentPriority + 1;
        for (auto x : op->getOperands()) {
          priority[x.defining] = currentPriority;
          prefer[x.defining] = op;
          phiOperand[op].push_back(x.defining);
        }
        currentPriority += 2;
      }
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

    std::unordered_set<Op*> active;
    for (const auto& event : events) {
      auto op = event.op;
      // Jumps will never interfere.
      if (isa<JOp>(op))
        continue;

      if (event.start) {
        for (Op* activeOp : active) {
          // FP and int are using different registers.
          // However, they are using the same stack,
          // so that must be taken into account when spilling.
          if (activeOp->getResultType() == Value::f32 ^ op->getResultType() == Value::f32) {
            spillInterf[op].insert(activeOp);
            spillInterf[activeOp].insert(op);
            continue;
          }

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

    std::unordered_set<Reg> bad, unpreferred;

    for (auto v : interf[op]) {
      // In the whole function, `sp` and `zero` are read-only.
      if (assignment.count(v) && assignment[v] != Reg::sp && assignment[v] != Reg::zero)
        bad.insert(assignment[v]);
    }

    if (isa<PhiOp>(op)) {
      // Dislike everything that might interfere with phi's operands.
      const auto &operands = phiOperand[op];
      for (auto x : operands) {
        for (auto v : interf[x]) {
          if (assignment.count(v) && assignment[v] != Reg::sp && assignment[v] != Reg::zero)
            unpreferred.insert(assignment[v]);
        }
      }
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
      if (!bad.count(rorder[i]) && !unpreferred.count(rorder[i])) {
        assignment[op] = rorder[i];
        break;
      }
    }

    // We have excluded too much. Try it again.
    if (!assignment.count(op) && unpreferred.size()) {
      for (int i = 0; i < rcnt; i++) {
        if (!bad.count(rorder[i])) {
          assignment[op] = rorder[i];
          break;
        }
      }
    }

    if (assignment.count(op))
      continue;

    spilled++;
    // Spilled. Try to see all spill offsets of conflicting ops.
    int desired = currentOffset;
    std::unordered_set<int> conflict;

    // Consider both `interf` (of the same register type)
    // and `spillInterf` (of different register type).
    for (auto v : interf[op]) {
      if (!spillOffset.count(v))
        continue;

      conflict.insert(spillOffset[v]);
    }
    for (auto v : spillInterf[op]) {
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

  // Only a single register is spilled. Let's use s11.
  if (highest == currentOffset) {
    for (auto [op, _] : spillOffset)
      assignment[op] = op->getResultType() == Value::f32 ? fspillReg : spillReg;
    spillOffset.clear();
  }

  // Allocate more stack space for it.
  if (spillOffset.size())
    STACKOFF(funcOp) = highest + 8;

  const auto getReg = [&](Op *op) {
    return assignment.count(op) ? assignment[op] :
      op->getResultType() == Value::f32 ? orderf[0] : order[0];
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
  LOWER(RemwOp, BINARY);
  LOWER(RemOp, BINARY);
  LOWER(BneOp, BINARY);
  LOWER(BeqOp, BINARY);
  LOWER(BltOp, BINARY);
  LOWER(BgeOp, BINARY);
  LOWER(StoreOp, BINARY);
  LOWER(AndOp, BINARY);
  LOWER(OrOp, BINARY);
  LOWER(XorOp, BINARY);
  LOWER(SltOp, BINARY);
  LOWER(FaddOp, BINARY);
  LOWER(FsubOp, BINARY);
  LOWER(FmulOp, BINARY);
  LOWER(FdivOp, BINARY);
  LOWER(FeqOp, BINARY);
  LOWER(FltOp, BINARY);
  LOWER(FleOp, BINARY);
  LOWER(SllwOp, BINARY);
  LOWER(SrlwOp, BINARY);
  LOWER(SrawOp, BINARY);
  LOWER(SraOp, BINARY);
  LOWER(SllOp, BINARY);
  LOWER(SrlOp, BINARY);
  
  LOWER(LoadOp, UNARY);
  LOWER(AddiwOp, UNARY);
  LOWER(AddiOp, UNARY);
  LOWER(SlliwOp, UNARY);
  LOWER(SrliwOp, UNARY);
  LOWER(SraiwOp, UNARY);
  LOWER(SraiOp, UNARY);
  LOWER(SlliOp, UNARY);
  LOWER(SrliOp, UNARY);
  LOWER(SeqzOp, UNARY);
  LOWER(SnezOp, UNARY);
  LOWER(SltiOp, UNARY);
  LOWER(AndiOp, UNARY);
  LOWER(OriOp, UNARY);
  LOWER(XoriOp, UNARY);
  LOWER(FcvtswOp, UNARY);
  LOWER(FcvtwsRtzOp, UNARY);
  LOWER(FmvwxOp, UNARY);

  // Note that some ops are dealt with later.
  // We can't remove all operands here.
  for (auto bb : region->getBlocks()) {
    for (auto op : bb->getOps()) {
      if (isa<PlaceHolderOp>(op) || isa<CallOp>(op) || isa<RetOp>(op))
        op->removeAllOperands();
    }
  }

  // Remove placeholders inserted previously.
  // We cannot directly erase that, otherwise `new`s might reuse the memory,
  // so that a newly constructed op might accidentally fall in `spillOffset`;
  // this means all ops must be erased at the end of the function.
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
    if (bb->succs.size() <= 1)
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
    builder.create<JOp>({ new TargetAttr(oldTarget) });

    // Create edge for else branch.
    auto ifnot = bbTerm->get<ElseAttr>();
    auto oldElse = ifnot->bb;
    ifnot->bb = edge2;

    builder.setToBlockEnd(edge2);
    builder.create<JOp>({ new TargetAttr(oldElse) });

    // Rename the blocks of the phis.
    for (auto succ : bb->succs) {
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

  // Don't forget that register 0 and offset 0 are the same.
#define SOFFSET(op, Ty) ((Reg) (-(op)->get<Spilled##Ty##Attr>()->offset-1))
#define SPILLABLE(op, Ty) (op->has<Ty##Attr>() ? op->get<Ty##Attr>()->reg : SOFFSET(op, Ty))

  // Detect circular copies and calculate a correct order.
  std::unordered_map<BasicBlock*, std::vector<std::pair<Reg, Reg>>> moveMap;
  std::unordered_map<BasicBlock*, std::map<std::pair<Reg, Reg>, Op*>> revMap;
  for (auto bb : bbs) {
    auto phis = bb->getPhis();

    std::vector<Op*> moves;
    for (auto phi : phis) {
      auto &ops = phi->getOperands();
      auto &attrs = phi->getAttrs();
      for (size_t i = 0; i < ops.size(); i++) {
        auto bb = FROM(attrs[i]);
        auto term = bb->getLastOp();
        builder.setBeforeOp(term);
        auto def = ops[i].defining;
        Op *mv;
        if (phi->getResultType() == Value::f32) {
          mv = builder.create<FmvOp>({
            new ImpureAttr,
            spillOffset.count(phi) ? (Attr*) new SpilledRdAttr GET_SPILLED_ARGS(phi) : RDC(getReg(phi)),
            spillOffset.count(def) ? (Attr*) new SpilledRsAttr GET_SPILLED_ARGS(def) : RSC(getReg(def))
          });
        } else {
          mv = builder.create<MvOp>({
            new ImpureAttr,
            spillOffset.count(phi) ? (Attr*) new SpilledRdAttr GET_SPILLED_ARGS(phi) : RDC(getReg(phi)),
            spillOffset.count(def) ? (Attr*) new SpilledRsAttr GET_SPILLED_ARGS(def) : RSC(getReg(def))
          });
        }
        moves.push_back(mv);
      }
    }

    std::copy(phis.begin(), phis.end(), std::back_inserter(allPhis));

    for (auto mv : moves) {
      auto dst = SPILLABLE(mv, Rd);
      auto src = SPILLABLE(mv, Rs);
      if (src == dst) {
        mv->erase();
        continue;
      }

      auto parent = mv->getParent();
      moveMap[parent].emplace_back(dst, src);
      revMap[parent][{ dst, src }] = mv;
    }
  }

  for (const auto &[bb, mvs] : moveMap) {
    std::unordered_map<Reg, Reg> moveGraph;
    for (auto [dst, src] : mvs)
      moveGraph[dst] = src;

    // Detect cycles.
    std::set<Reg> visited, visiting;
    std::vector<std::pair<Reg, Reg>> sorted;
    // Cycle headers.
    std::vector<Reg> headers;
    // All members in the cycle under a certain header.
    std::unordered_map<Reg, std::vector<Reg>> members;
    // All nodes that are in a certain cycle.
    std::unordered_set<Reg> inCycle;

    // Do a topological sort; it will decide whether there's a cycle.
    std::function<void(Reg)> dfs = [&](Reg node) {
      visiting.insert(node);
      Reg src = moveGraph[node];

      if (visiting.count(src))
        // A node is visited twice. Here's a cycle.
        headers.push_back(node);
      else if (!visited.count(src) && moveGraph.count(src))
        dfs(src);
    
      visiting.erase(node);
      visited.insert(node);
      sorted.emplace_back(node, src);
    };

    for (auto [dst, src] : mvs) {
      if (!visited.count(dst))
        dfs(dst);
    }

    std::reverse(sorted.begin(), sorted.end());

    // Fill in record of cycles.
    for (auto header : headers) {
      Reg runner = header;
      do {
        members[header].push_back(runner);
        runner = moveGraph[runner];
      } while (runner != header);

      for (auto member : members[header])
        inCycle.insert(member);
    }

    // Move sorted phis so that they're in the correct order.
    Op* term = bb->getLastOp();

    std::unordered_set<Reg> emitted;

    for (auto [dst, src] : sorted) {
      if (dst == src || emitted.count(dst) || inCycle.count(dst))
        continue;

      revMap[bb][{ dst, src }]->moveBefore(term);
      emitted.insert(dst);
    }

    if (members.empty())
      continue;

    // Looks like still buggy? No idea why.
    // Anyway, disable it for now.
    std::cerr << "remark: cycle detected\n";
    assert(false);
  }

  // Erase all phi's properly. There might be cross-reference across blocks,
  // so we need to remove all operands first.
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
          op->add<SpilledRdAttr> GET_SPILLED_ARGS(op);
      }
    }
  }

  // Deal with spilled variables.
  std::vector<Op*> remove;
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
        // We will rematerialize them later.
        if (isa<LiOp>(rd->ref) || isa<LaOp>(rd->ref)) {
          remove.push_back(op);
          continue;
        }

        int offset = delta + rd->offset;
        bool fp = rd->fp;
        auto reg = fp ? fspillReg : spillReg;

        builder.setAfterOp(op);
        if (offset < 2048)
          builder.create<StoreOp>({ RSC(reg), RS2C(Reg::sp), new IntAttr(offset), new SizeAttr(8) });
        else if (offset < 4096) {
          builder.create<AddiOp>({ RDC(spillReg2), RSC(Reg::sp), new IntAttr(2047), new SizeAttr(8) });
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
        // Rematerialized.
        auto ref = rs->ref;
        if (isa<LiOp>(ref))
          builder.create<LiOp>({ RDC(reg), new IntAttr(V(ref)) });
        else if (isa<LaOp>(ref))
          builder.create<LaOp>({ RDC(reg), new NameAttr(NAME(ref)) });
        else if (offset < 2048)
          builder.create<LoadOp>(ldty, { RDC(reg), RSC(Reg::sp), new IntAttr(offset), new SizeAttr(8) });
        else if (offset < 4096) {
          builder.create<AddiOp>({ RDC(spillReg), RSC(Reg::sp), new IntAttr(2047), new SizeAttr(8) });
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
        // Rematerialized.
        auto ref = rs2->ref;
        if (isa<LiOp>(ref))
          builder.create<LiOp>({ RDC(reg), new IntAttr(V(ref)) });
        else if (isa<LaOp>(ref))
          builder.create<LaOp>({ RDC(reg), new NameAttr(NAME(ref)) });
        else if (offset < 2048)
          builder.create<LoadOp>(ldty, { RDC(reg), RSC(Reg::sp), new IntAttr(offset), new SizeAttr(8) });
        else if (offset < 4096) {
          builder.create<AddiOp>({ RDC(spillReg2), RSC(Reg::sp), new IntAttr(2047), new SizeAttr(8) });
          builder.create<LoadOp>(ldty, { RDC(reg), RSC(spillReg2), new IntAttr(offset - 2047), new SizeAttr(8) });
        }
        else assert(false);
        op->add<Rs2Attr>(reg);
      }
    }
  }

  for (auto op : remove)
    op->erase();
}

void RegAlloc::run() {
  auto funcs = collectFuncs();
  fnMap = getFunctionMap();
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
