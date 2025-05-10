#include "LoopPasses.h"
#include "Passes.h"

using namespace sys;

int clamp(int64_t x) {
  if (x > INT_MAX)
    return INT_MAX;
  if (x < INT_MIN)
    return INT_MIN;
  return x;
}

using IRange = std::pair<int, int>;

IRange join(IRange l, IRange r, bool widen) {
  auto [a1, b1] = l;
  auto [a2, b2] = r;
  if (widen)
    return std::make_pair(a2 < a1 ? INT_MIN : a1, b1 < b2 ? INT_MAX : b1);
  return std::make_pair(std::max(a1, a2), std::max(b1, b2));
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
        if (xrange.first == range.first && xrange.second == range.second) \
          return false; \
        op->remove<RangeAttr>(); \
      } \
      op->add<RangeAttr>(range); \
      return true; \
    } \
  }

#define UPDATE_BOOL_RANGE(Ty) \
  if (isa<Ty>(op)) { \
    if (!op->has<RangeAttr>()) { \
      op->add<RangeAttr>(0, 1); \
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
  int64_t x[] = { a1 / a2, a1 / b2, b1 / a2, b1 / b2 };
  return *std::min_element(x, x + 4);
}

int64_t maxdiv(int64_t a1, int64_t b1, int64_t a2, int64_t b2) {
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

bool calculateRange(Op *op, bool widen) {
  if (isa<IntOp>(op)) {
    if (op->has<RangeAttr>())
      return false;

    int value = V(op);
    op->add<RangeAttr>(value, value);
    return true;
  }

  UPDATE_RANGE(AddIOp, ((int64_t) a1) + a2, ((int64_t) b1) + b2);
  UPDATE_RANGE(SubIOp, ((int64_t) a1) - a2, ((int64_t) b1) - b2);
  UPDATE_RANGE(MulIOp, minmul(a1, b1, a2, b2), maxmul(a1, b1, a2, b2));
  UPDATE_RANGE(DivIOp, mindiv(a1, b1, a2, b2), maxdiv(a1, b1, a2, b2));
  UPDATE_RANGE(ModIOp, minmod(a1, b1, a2, b2), maxmod(a1, b1, a2, b2));

  UPDATE_BOOL_RANGE(EqOp);
  UPDATE_BOOL_RANGE(LeOp);
  UPDATE_BOOL_RANGE(LtOp);
  UPDATE_BOOL_RANGE(NeOp);
  UPDATE_BOOL_RANGE(NotOp);
  UPDATE_BOOL_RANGE(SetNotZeroOp);
  
  if (isa<PhiOp>(op)) {
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
      range->range = join(range->range, { min, max }, /*widen=*/true);
      return true;
    }
    // Unknown.
    if (min == INT_MAX && max == INT_MIN)
      return false;
    else
      op->add<RangeAttr>(min, max);
  }

  return false;
}

void Range::runImpl(Region *region, const LoopForest &forest) {
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
        auto ret = bb->getLastOp()->getOperand();
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

  // Convert to extended SSA.
  // E-SSA is first described here: https://dl.acm.org/doi/pdf/10.1145/358438.349342
  // Also refer to the SSA book, Chapter 13.
  //
  // First we need to find out the variables used for condition.
  
}

void Range::run() {
  LoopAnalysis loop(module);
  loop.run();
  auto forests = loop.getResult();

  auto funcs = collectFuncs();

  for (auto func : funcs)
    runImpl(func->getRegion(), forests[func]);

  // Destruct the auxiliary ops we've introduced.
  runRewriter([&](IdOp *op) {
    auto def = op->getOperand().defining;
    op->replaceAllUsesWith(def);
    op->erase();
    return true;
  });
}
