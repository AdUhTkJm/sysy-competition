#include "ArmPasses.h"

using namespace sys;
using namespace sys::arm;

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
static const Reg vspillReg = Reg::v31;
static const Reg vspillReg2 = Reg::v15;

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
static const Reg vleafOrder[] = {
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
static const Reg vnormalOrder[] = {
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

constexpr int regcount = 24;
constexpr int vregcount = 30;

namespace {

struct Event {
  int timestamp;
  bool start;
  Op *op;
};

}

// The base algorithm is quite similar to that in RISC-V backend.
void RegAlloc::allocate(Region *region, bool isLeaf) {
  region->updateLiveness();

  const Reg *order = isLeaf ? leafOrder : normalOrder;
  const Reg *vorder = isLeaf ? vleafOrder : vnormalOrder;

  Builder builder;


  auto funcOp = region->getParent();

  // First of all, add 35 precolored placeholders before each call.
  // This denotes that a CallOp clobbers those registers.
  runRewriter(funcOp, [&](CallOp *op) {
    builder.setBeforeOp(op);
    for (auto reg : callerSaved) {
      auto placeholder = builder.create<PlaceHolderOp>();
      assignment[placeholder] = reg;
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
      builder.replace<ReadXRegOp>(op, { new RegAttr(argRegs[cnt]) });
      cnt++;
      continue;
    }
    // Spilled to stack; don't do anything.
  }

  // If a phi has an operand of float type, then itself must also be of float type.
  runRewriter(funcOp, [&](PhiOp *op) {
    for (auto operand : op->getOperands()) {
      if (operand.defining->getResultType() == Value::f32) {
        op->setResultType(Value::f32);
        return false;
      }
    }
    // Do it only once.
    return false;
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
        assignment[op] = REG(op);
        priority[op] = 1;
      }
      if (isa<ReadRegOp>(op) || isa<ReadXRegOp>(op) || isa<ReadFRegOp>(op)) {
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
      if (!hasRd(op))
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
    if (assignment.count(op) || !hasRd(op))
      continue;

    std::set<Reg> bad;
    for (auto v : interf[op]) {
      // In the whole function, `sp` and `zero` are read-only.
      if (assignment.count(v))
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

    auto rcnt = op->getResultType() != Value::f32 ? regcount : vregcount;
    auto rorder = op->getResultType() != Value::f32 ? order : vorder;

    for (int i = 0; i < rcnt; i++) {
      if (!bad.count(rorder[i])) {
        assignment[op] = rorder[i];
        break;
      }
    }

    if (assignment.count(op))
      continue;

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
      op->getResultType() == Value::f32 ? vorder[0] : order[0];
  };
}

void RegAlloc::run() {
  auto funcs = collectFuncs();

  for (auto func : funcs) {
    auto calls = func->findAll<BrOp>();
    auto region = func->getRegion();

    allocate(region, calls.size() == 0);
  }
}
