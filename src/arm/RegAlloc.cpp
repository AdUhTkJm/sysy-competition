#include "ArmAttrs.h"
#include "ArmPasses.h"

using namespace sys;
using namespace sys::arm;

static const Reg fregs[] = {
  Reg::v0, Reg::v1, Reg::v2, Reg::v3,
  Reg::v4, Reg::v5, Reg::v6, Reg::v7,
};
static const Reg argregs[] = {
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

  // If a phi has an operand of float type, then itself must also be of float type.
  runRewriter(funcOp, [&](PhiOp *op) {
    for (auto operand : op->getOperands()) {
      if (operand.ty == Value::f32) {
        op->setResultType(Value::f32);
        return false;
      }
    }
    return false;
  });

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
      if (isa<CopyToOp>(op)) {
        assignment[op] = PRECOLOR(op);
        priority[op] = 1;
      }
      if (isa<CopyFromOp>(op))
        priority[op] = 1;

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
      if (event.start) {
        auto op = event.op;

        for (Op* activeOp : active) {
          // FP and int are using different registers.
          if (activeOp->getResultType() == Value::f32 ^ op->getResultType() == Value::f32)
            continue;

          interf[op].insert(activeOp);
          interf[activeOp].insert(op);

          // For special instructions, we also mark some registers as forbidden.
          if (isa<CallOp>(op)) {
            auto &bad = forbidden[op];
            std::copy(callerSaved.begin(), callerSaved.end(), std::inserter(bad, bad.begin()));
          }

          if (isa<GetArgOp>(op)) {
            auto &bad = forbidden[op];
            auto index = V(op);
            if (index < 8)
              bad.insert(argregs[index]);
          }
        }

        active.insert(op);
      } else
        active.erase(event.op);
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

  int highest = 0;
  
  for (auto op : ops) {
    // Do not allocate colored instructions.
    if (assignment.count(op))
      continue;

    std::set<Reg> bad = forbidden[op];
    for (auto v : interf[op]) {
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
      if (isa<CopyToOp>(use)) {
        auto reg = PRECOLOR(op);
        if (!bad.count(reg)) {
          preferred = (int) reg;
          break;
        }
      }
    }
    if (isa<CopyFromOp>(op)) {
      auto reg = PRECOLOR(op);
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
    int desired = 0;
    std::set<int> conflict;
    for (auto v : interf[op]) {
      if (!spillOffset.count(v))
        continue;

      conflict.insert(spillOffset[v]);
    }

    // Try find a space.
    for (auto offset : conflict) {
      if (desired == offset)
        desired += 8;
    }

    // Record the offset.
    spillOffset[op] = desired;

    // Update `highest`, which will indicate the size allocated.
    if (desired > highest)
      highest = desired;
  }

  STACKOFF(funcOp) += highest;
}

void RegAlloc::run() {
  auto funcs = collectFuncs();

  for (auto func : funcs) {
    auto calls = func->findAll<BrOp>();
    allocate(func->getRegion(), calls.size() == 0);
  }
}
