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
  
  // Sub
  "(change (sub 'a 'b) (!sub 'a 'b))",
  "(change (sub x 0) x)",
  "(change (sub x (minus y)) (add x y))",

  // Mul
  "(change (mul 'a 'b) (!mul 'a 'b))",

  // Div
  "(change (div 'a 'b) (!div 'a 'b))",

  // And
  "(change (and 'a 'b) (!and 'a 'b))",
  "(change (and x (eq x 'a)) (!only-if (!ne 'a 0) (eq x 'a)))",

  // Mod
  "(change (mod 'a 'b) (!mod 'a 'b))",

  // Lsh
  "(change (lsh 'a 'b) (!lsh 'a 'b))",

  // Eq
  "(change (eq 'a 'b) (!eq 'a 'b))",
  "(change (eq x 0) (not x))",

  // Not
  "(change (not 'a) (!not 'a))",
  "(change (not (eq x y)) (ne x y))",

  // Lt
  "(change (lt 'a 'b) (!lt 'a 'b))",

  // Ite
  "(change (ite (not x) y z) (ite x z y))",
  "(change (ite 0 y z) z)",
  "(change (ite 'a y z) (!only-if (!ne 'a 0) y))",

  // Mulmod
  "(change (mulmod 'a 'b 'c) (!mulmod 'a 'b 'c))",
  "(change (mulmod x y 1) 0)",
  "(change (mulmod x y -1) 0)",
};

BvExpr *rewriteRoot(BvExpr *expr, BvExprContext &ctx) {
  // x % 2 == x - (x[31] + x) & (-2)
  if (expr->ty == BvExpr::Mod && expr->r->ty == BvExpr::Const && expr->r->vi == 2) {
    auto _1 = expr->l;
    auto _2 = ctx.create(BvExpr::Extr, _1, 31);
    auto _3 = ctx.create(BvExpr::Add, _1, _2);
    auto _4 = ctx.create(BvExpr::Const, -2);
    auto _5 = ctx.create(BvExpr::And, _3, _4);
    auto _6 = ctx.create(BvExpr::Sub, _1, _5);
    return _6;
  }

  // extr of constant
  if (expr->ty == BvExpr::Extr && expr->l->ty == BvExpr::Const) {
    return ctx.create(BvExpr::Const, (((unsigned) expr->l->vi) >> expr->vi) & 1);
  }

  return expr;
}

BvExpr *rewrite(BvExpr *expr, BvExprContext &ctx) {
  if (!expr)
    return nullptr;

  BvExpr* newcond = rewrite(expr->cond, ctx);
  BvExpr* newl = rewrite(expr->l, ctx);
  BvExpr* newr = rewrite(expr->r, ctx);

  BvExpr* updated = ctx.create(expr->ty, expr->vi, expr->name, newcond, newl, newr);
  return rewriteRoot(updated, ctx);
}

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
      if (auto rewritten = rewrite(expr, ctx); rewritten != expr)
        changed = true, expr = rewritten;
    }
    result = expr;
  } while (changed);

  std::cerr << "simplified: " << expr << "\n";
  return result;
}
