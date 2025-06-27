#include "CleanupPasses.h"
#include "../utils/Matcher.h"

using namespace sys;

std::map<std::string, int> RangeAwareFold::stats() {
  return {
    { "folded-ops", folded }
  };
}

// Defined in Specialize.cpp.
void removeRange(Region *region);

void RangeAwareFold::run() {
  Builder builder;

  // Fold left/right shifts early.
  runRewriter([&](DivIOp *op) {
    auto x = op->DEF(0);
    auto y = op->DEF(1);
    if (!isa<IntOp>(y) || V(y) < 0 || !x->has<RangeAttr>())
      return false;

    auto [low, high] = RANGE(x);
    if (low < 0)
      return false;

    if (__builtin_popcount(V(y)) != 1)
      return false;

    // This can be replaced to an ordinary right-shift.
    folded++;
    builder.setBeforeOp(op);
    auto vi = builder.create<IntOp>({ new IntAttr(__builtin_ctz(V(y))) });
    builder.replace<RShiftOp>(op, { x, vi });
    return false;
  });

  runRewriter([&](ModIOp *op) {
    auto x = op->DEF(0);
    auto y = op->DEF(1);
    if (!isa<IntOp>(y) || !x->has<RangeAttr>())
      return false;

    if (V(y) < 0)
      V(y) = -V(y);

    auto [low, high] = RANGE(x);
    if (low < 0)
      return false;

    if (__builtin_popcount(V(y)) != 1)
      return false;

    // Replace with bit-and.
    folded++;
    builder.setBeforeOp(op);
    auto vi = builder.create<IntOp>({ new IntAttr(V(y) - 1) });
    builder.replace<AndIOp>(op, { x, vi });
    return false;
  });

  Rule eq_or("(or (eq x 1) (not x))");
  runRewriter([&](OrIOp *op) {
    if (eq_or.match(op)) {
      auto x = eq_or.extract("x");
      if (!x->has<RangeAttr>())
        return false;
      auto [low, high] = RANGE(x);
      if (low == 0) {
        folded++;
        builder.setBeforeOp(op);
        auto two = builder.create<IntOp>({ new IntAttr(2) });
        builder.replace<LtOp>(op, { x, two });
        return false;
      }
    }
    return false;
  });
  
  auto funcs = collectFuncs();

  for (auto func : funcs) {
    auto region = func->getRegion();
    removeRange(region);
  }
}
