#include "BvMatcher.h"
#include "SMT.h"

using namespace smt;

namespace {

BvRule rules[] = {
  // Add
  "(change (add 'a 'b) (!add 'a 'b))",
  "(change (add x 0) x)",
  "(change (add 'a x) (add x 'a))",
  "(change (add x x) (lsh x 1))",

  // And
  "(change (and 'a 'b) (!and 'a 'b))",
  "(change (and 1 x) x)",
  "(change (and x 1) x)",
  "(change (and x (eq x 'a)) (!only-if (!ne 'a 0) (eq x 'a)))",

  // Mod
  "(change (mod 'a 'b) (!mod 'a 'b))",

  // Lsh
  "(change (lsh 'a 'b) (!lsh 'a 'b))",

  // Ite
  "(change (ite (not x) y z) (ite x z y))",

  // Mulmod
  "(change (mulmod x y 1) 0)",
  "(change (mulmod x y -1) 0)",
};

}

[[nodiscard]]
BvExpr *smt::simplify(BvExpr *expr, BvExprContext &ctx) {
  BvExpr *result = expr;
  bool changed;
  for (auto &rule : rules)
    rule.ctx = &ctx;
  do {
    changed = false;
    for (auto &rule : rules) {
      if (auto rewritten = rule.rewrite(expr); rewritten != expr)
        changed = true, expr = rewritten;
    }
    result = expr;
  } while (changed);

  std::cerr << "simplified: " << expr << "\n";
  return result;
}
