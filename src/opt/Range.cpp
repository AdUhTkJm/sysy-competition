#include "Analysis.h"

using namespace sys;

namespace {

int clamp(int64_t x) {
  if (x > INT_MAX)
    return INT_MAX;
  if (x < INT_MIN)
    return INT_MIN;
  return x;
}

using IRange = std::pair<int, int>;

IRange join(IRange l, IRange r, bool widen = false) {
  auto [a1, b1] = l;
  auto [a2, b2] = r;
  if (widen)
    return std::make_pair(a2 < a1 ? INT_MIN : a1, b1 < b2 ? INT_MAX : b1);
  return std::make_pair(std::min(a1, a2), std::max(b1, b2));
}

#define UPDATE_RANGE(Ty, Low, High) \
  if (isa<Ty>(op)) { \
    auto l = op->getOperand(0).defining; \
    auto r = op->getOperand(1).defining; \
    if (l->has<RangeAttr>() && r->has<RangeAttr>()) { \
      auto [a1, b1] = RANGE(l); \
      auto [a2, b2] = RANGE(r); \
      auto range = std::make_pair(clamp(Low), clamp(High)); \
      if (auto rangeAttr = op->find<RangeAttr>()) { \
        auto xrange = join(range, rangeAttr->range, false); \
        if (xrange == rangeAttr->range) \
          return false; \
        op->remove<RangeAttr>(); \
        op->add<RangeAttr>(xrange); \
      } else \
        op->add<RangeAttr>(range); \
      return true; \
    } else if (!op->has<RangeAttr>()) { \
      op->add<RangeAttr>(/*unknown*/); \
      return true; \
    } \
  }

#define UPDATE_BOOL_RANGE(Ty, valid, unsat) \
  if (isa<Ty>(op)) { \
    auto l = op->getOperand(0).defining; \
    auto r = op->getOperand(1).defining; \
    if (l->has<RangeAttr>() && r->has<RangeAttr>()) { \
      auto [a1, b1] = RANGE(l); \
      auto [a2, b2] = RANGE(r); \
      auto range = valid ? IRange { 1, 1 } : unsat ? IRange { 0, 0 } : IRange { 0, 1 }; \
      if (auto rangeAttr = op->find<RangeAttr>()) { \
        auto xrange = join(range, rangeAttr->range, false); \
        if (xrange == rangeAttr->range) \
          return false; \
        op->remove<RangeAttr>(); \
        op->add<RangeAttr>(xrange); \
      } else \
        op->add<RangeAttr>(range); \
      return true; \
    } else if (!op->has<RangeAttr>()) { \
      op->add<RangeAttr>(IRange { 0, 1 }); \
      return true; \
    } \
  }

int64_t minmul(int a1, int b1, int a2, int b2) {
  __int128_t x[] = { ((__int128_t) a1) * a2, ((__int128_t) a1) * b2, ((__int128_t) b1) * a2, ((__int128_t) b1) * b2 };
  auto min = *std::min_element(x, x + 4);
  if (min < INT_MIN)
    return INT_MIN;
  return min;
}

int64_t maxmul(int a1, int b1, int a2, int b2) {
  __int128_t x[] = { ((__int128_t) a1) * a2, ((__int128_t) a1) * b2, ((__int128_t) b1) * a2, ((__int128_t) b1) * b2 };
  auto max = *std::max_element(x, x + 4);
  if (max > INT_MAX)
    return INT_MAX;
  return max;
}

int64_t mindiv(int64_t a1, int64_t b1, int64_t a2, int64_t b2) {
  if (a2 == 0 || b2 == 0)
    return INT_MIN;
  if (a2 * b2 < 0)
    return -std::max(std::abs(a1), std::abs(a2));
  
  int64_t x[] = { a1 / a2, a1 / b2, b1 / a2, b1 / b2 };
  return *std::min_element(x, x + 4);
}

int64_t maxdiv(int64_t a1, int64_t b1, int64_t a2, int64_t b2) {
  if (a2 == 0 || b2 == 0)
    return INT_MAX;
  if (a2 * b2 < 0)
    return std::max(std::abs(a1), std::abs(a2));
  
  int64_t x[] = { a1 / a2, a1 / b2, b1 / a2, b1 / b2 };
  return *std::max_element(x, x + 4);
}

int minmod(int a1, int b1, int a2, int b2) {
  if (a1 >= 0 && a2 > 0)
    return 0;
  if (a1 >= 0 && a2 > b1)
    return a1;
  return -std::max(std::abs(a2), std::abs(b2)) + 1;
}

int maxmod(int a1, int b1, int a2, int b2) {
  if (a1 >= 0 && a2 > b1)
    return b1;
  return std::max(std::abs(a2), std::abs(b2)) - 1;
}

#define NORANGE(Ty) || isa<Ty>(op)
bool norange(Op *op) {
  return isa<AllocaOp>(op)
    NORANGE(GotoOp)
    NORANGE(BranchOp)
    NORANGE(ReturnOp)
    NORANGE(StoreOp)
    NORANGE(CloneOp)
    NORANGE(JoinOp)
  ;
}

int nowiden;

// Check phi nodes created by Range::split().
bool updateConditional(Op *op, bool &changed) {
  if (op->getOperandCount() != 1)
    return false;
  
  auto parent = op->getParent();
  auto pred = FROM(op->getAttrs()[0]);
  
  // Check whether this op is a condition.
  auto x = op->DEF();
  if (!x->has<RangeAttr>())
    return false;

  auto term = pred->getLastOp();
  if (!isa<BranchOp>(term))
    return false;

  bool isTarget = parent == TARGET(term);

  auto cond = term->DEF(0);
  // TODO: more comparison types (also change Range.cpp)
  if (!isa<LtOp>(cond))
    return false;

  if (cond->DEF(0) != x)
    return false;

  auto y = cond->DEF(1);
  if (!y->has<RangeAttr>())
    return false;

  // Now this is of form `x < y`.
  auto [xlow, xhigh] = RANGE(x);
  auto [ylow, yhigh] = RANGE(y);

  IRange r = isTarget
    ? IRange { xlow, std::min(xhigh, yhigh - 1) } // x < y
    : IRange { std::max(xlow, ylow), xhigh };     // x >= y
  if (!op->has<RangeAttr>()) {
    op->add<RangeAttr>(r);
    changed = true;
    return true;
  }

  auto &rop = RANGE(op);
  if (rop != r) {
    changed = true;
    rop = r;
  }
  return true;
}

bool calculateRange(Op *op) {
  if (isa<IntOp>(op)) {
    if (op->has<RangeAttr>())
      return false;

    int value = V(op);
    op->add<RangeAttr>(value, value);
    return true;
  }

  if (isa<F2IOp>(op)) {
    if (op->has<RangeAttr>())
      return false;
    
    op->add<RangeAttr>(/*unknown*/);
    return true;
  }
  
  if (isa<CallOp>(op)) {
    // We know the semantics of external calls.
    const auto &name = op->getName();
    if (name == "getch")
      op->add<RangeAttr>(1, 128);
    else if (name == "getarray" || name == "getfarray")
      op->add<RangeAttr>(1, INT_MAX);
    else if (!op->has<RangeAttr>())
      op->add<RangeAttr>(/*unknown*/);

    return false;
  }

  UPDATE_RANGE(AddIOp, ((int64_t) a1) + a2, ((int64_t) b1) + b2);
  UPDATE_RANGE(SubIOp, ((int64_t) a1) - b2, ((int64_t) b1) - a2);
  UPDATE_RANGE(MulIOp, minmul(a1, b1, a2, b2), maxmul(a1, b1, a2, b2));
  UPDATE_RANGE(DivIOp, mindiv(a1, b1, a2, b2), maxdiv(a1, b1, a2, b2));
  UPDATE_RANGE(ModIOp, minmod(a1, b1, a2, b2), maxmod(a1, b1, a2, b2));

  UPDATE_BOOL_RANGE(EqOp, (a1 == a2 && b1 == b2 && a1 == b1), (a1 > b2 || a2 > b1));
  UPDATE_BOOL_RANGE(LeOp, (b1 <= a2), (a1 > b2));
  UPDATE_BOOL_RANGE(LtOp, (b1 < a2), (a1 >= b2));
  UPDATE_BOOL_RANGE(NeOp, (a1 > b2 || a2 > b1), (a1 == a2 && b1 == b2 && a1 == b1));

  if (isa<NotOp>(op) || isa<SetNotZeroOp>(op)) {
    if (!op->has<RangeAttr>()) {
      op->add<RangeAttr>(IRange { 0, 1 });
      return true;
    }
    return false;
  }
  
  if (isa<PhiOp>(op)) {
    bool changed = false;
    bool success = updateConditional(op, changed);
    if (changed)
      return true;
    if (success)
      return false;
    // Proceed only when neither successful nor changed.

    int min = INT_MAX, max = INT_MIN;
    for (auto operand : op->getOperands()) {
      auto def = operand.defining;
      if (auto range = def->find<RangeAttr>()) {
        auto [a1, b1] = range->range;
        min = std::min(min, a1);
        max = std::max(max, b1);
      }
    }
    if (auto range = op->find<RangeAttr>()) {
      auto [a1, b1] = range->range;
      if (a1 == min && b1 == max)
        return false;
      auto last = range->range;
      range->range = join(range->range, { min, max }, /*widen=*/!nowiden);
      return range->range != last;
    }
    // Unknown.
    if (min == INT_MAX && max == INT_MIN)
      return false;
    op->add<RangeAttr>(min, max);
    return true;
  }

  if (!op->has<RangeAttr>() && !norange(op))
    op->add<RangeAttr>(/*unknown*/);
  return false;
}

void removeRange(Region *region) {
  for (auto bb : region->getBlocks()) {
    for (auto op : bb->getOps())
      op->remove<RangeAttr>();
  }
}

}

void Range::postdom(Region *region) {
  // First make sure the region has only a single exit.
  std::vector<BasicBlock*> exits;
  for (auto bb : region->getBlocks()) {
    if (isa<ReturnOp>(bb->getLastOp()))
      exits.push_back(bb);
  }
  if (exits.size() > 1) {
    Builder builder;
    auto exit = region->appendBlock();
    builder.setToBlockStart(exit);

    // We have a return value. Create a phi to record it.
    if (exits[0]->getLastOp()->getOperands().size() > 0) {
      auto phi = builder.create<PhiOp>();
      for (auto bb : exits) {
        auto last = bb->getLastOp();
        // We might insert an empty return in codegen.
        if (!last->getOperandCount())
          continue;
        auto ret = last->getOperand();
        phi->pushOperand(ret);
        phi->add<FromAttr>(bb);
      }
      builder.create<ReturnOp>({ phi });
    } else {
      // Just a normal return.
      builder.create<ReturnOp>();
    }

    // Rewire all exits to the new exit.
    for (auto bb : exits)
      builder.replace<GotoOp>(bb->getLastOp(), { new TargetAttr(exit) });
  }

  // Now we can calculate the post-domination tree.
  region->updatePDoms();
  region->updateDoms();
}

void Range::split(Region *region) {
  Builder builder;

  for (auto bb : region->getBlocks()) {
    auto term = bb->getLastOp();
    if (!isa<BranchOp>(term))
      continue;

    auto cond = term->DEF();
    if (!isa<LtOp>(cond))
      continue;

    auto x = cond->DEF(0);

    auto bb1 = TARGET(term), bb2 = ELSE(term);

    // Make sure this isn't the branch of a while loop.
    // Unrotated loops won't matter.

    // For rotated loops with a single block, this would fail.
    if (bb1 == bb || bb2 == bb)
      continue;
    // For rotated loops with separate header and latch, this fails,
    // as latch jumps to header but it doesn't dominate header.
    if (!bb->dominates(bb1) || !bb->dominates(bb2))
      continue;

    // Have a look whether we've already split it first.
    // Don't do it repeatedly.
    int found = 0;
    auto bb1phis = bb1->getPhis();
    for (auto phi : bb1phis) {
      if (phi->getOperandCount() == 1 && phi->DEF() == x) {
        found++;
        break;
      }
    }
    auto bb2phis = bb2->getPhis();
    for (auto phi : bb2phis) {
      if (phi->getOperandCount() == 1 && phi->DEF() == x) {
        found++;
        break;
      }
    }
    if (found == 2)
      continue;

    // Now give a phi for both successors.
    builder.setToBlockStart(bb1);
    auto x1 = builder.create<PhiOp>({ x }, { new FromAttr(bb) });
    builder.setToBlockStart(bb2);
    auto x2 = builder.create<PhiOp>({ x }, { new FromAttr(bb) });

    // Rename operations.
    auto uses = x->getUses();
    for (auto use : uses) {
      if (use == x1 || use == x2)
        continue;

      auto parent = use->getParent();
      // Both branch get to here; use the original `x` instead.
      if (parent->postDominates(bb1) && parent->postDominates(bb2))
        continue;
      if (bb1->dominates(parent))
        use->replaceOperand(x, x1);
      if (bb2->dominates(parent))
        use->replaceOperand(x, x2);
    }
  }
}

void Range::analyze(Region *region) {
  bool changed;
  nowiden = 4;
  do {
    changed = false;
    nowiden--;
    for (auto bb : region->getBlocks()) {
      for (auto op : bb->getOps())
        changed |= calculateRange(op);
    }
  } while (changed);
}

void Range::run() {
  auto funcs = collectFuncs();

  for (auto func : funcs) {
    auto region = func->getRegion();
    removeRange(region);
    postdom(region);
    split(region);
    analyze(region);
  }

  auto calls = module->findAll<CallOp>();
  // Find range of call arguments.
  // Maps [funcname, argId] to the range.
  // TODO: for recursive functions, perhaps we can improve this?
  std::map<std::pair<std::string, int>, IRange> argrange;
  for (auto call : calls) {
    const auto &name = NAME(call);
    if (isExtern(name))
      continue;
    
    for (int i = 0; i < call->getOperandCount(); i++) {
      auto def = call->DEF(i);
      if (!def->has<RangeAttr>()) {
        argrange[{ name, i }] = { INT_MIN, INT_MAX };
        break;
      }

      auto &range = argrange[{ name, i }];
      range = join(range, RANGE(def));
    }
  }

  for (auto func : funcs) {
    int argcnt = func->get<ArgCountAttr>()->count;
    if (argcnt == 0)
      continue;

    auto region = func->getRegion();
    removeRange(region);

    const auto &name = NAME(func);
    auto getargs = func->findAll<GetArgOp>();
    std::unordered_map<int, Op*> getarg;
    for (auto g : getargs)
      getarg[V(g)] = g;

    for (int i = 0; i < argcnt; i++) {
      if (!argrange.count({name, i}))
        getarg[i]->add<RangeAttr>(IRange { INT_MIN, INT_MAX });
      else
        getarg[i]->add<RangeAttr>(argrange[{ name, i }]);
    }

    analyze(region);
  }
}
