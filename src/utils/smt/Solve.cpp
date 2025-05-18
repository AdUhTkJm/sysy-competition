#include "SMT.h"
#include <cassert>

using namespace smt;

BvSolver::BvSolver() {
  _false = ctx.create();
  _true = ctx.create();

  // Add (!_false) to ensure _false is false.
  reserve({ ctx.neg(_false) });
  // Similarly add (_true) to ensure _true is true.
  reserve({ ctx.pos(_true) });
}

// Note all these are bidirectional encodings.

void BvSolver::addAnd(Variable out, Variable a, Variable b) {
  // Meaning:
  //   out -> a
  //   out -> b
  //   a & b -> out
  reserve({ ctx.neg(a), ctx.neg(b), ctx.pos(out) });
  reserve({ ctx.pos(a), ctx.neg(out) });
  reserve({ ctx.pos(b), ctx.neg(out) });
}

void BvSolver::addOr(Variable out, Variable a, Variable b) {
  // Meaning:
  //   out -> a | b
  //   a -> out
  //   b -> out
  reserve({ ctx.pos(a), ctx.pos(b), ctx.neg(out) });
  reserve({ ctx.neg(a), ctx.pos(out) });
  reserve({ ctx.neg(b), ctx.pos(out) });
}

void BvSolver::addXor(Variable out, Variable a, Variable b) {
  // Meaning:
  //   out, a, b -> false
  //   out -> a | b
  //   b -> a | out
  //   a -> b | out
  // (A bit mind-bending.)
  reserve({ ctx.neg(a), ctx.neg(b), ctx.neg(out) });
  reserve({ ctx.pos(a), ctx.pos(b), ctx.neg(out) });
  reserve({ ctx.pos(a), ctx.neg(b), ctx.pos(out) });
  reserve({ ctx.neg(a), ctx.pos(b), ctx.pos(out) });
}

Bitvector BvSolver::blastAdd(const Bitvector &a, const Bitvector &b) {
  // Consider a CLA adder.
  //   g[i] = a[i] & b[i]: "Generate", means to generate a carry;
  //   p[i] = a[i] ^ b[i]: "Propagate", means the carry from prev. bit will propagate through.
  // Refer to Cambridge Part IA, Digital Electronics.
  // (Perhaps I need some revision.)
  Bitvector g(32), p(32), result(32);

  for (int i = 0; i < 32; i++) {
    g[i] = ctx.create();
    p[i] = ctx.create();
    addAnd(g[i], a[i], b[i]);
    addXor(p[i], a[i], b[i]);
  }

  // According to the meaning described above,
  // Carry c[i+1] = g[i] | (c[i] & p[i])
  Bitvector c(32);
  c[0] = _false;
  for (int i = 0; i < 31; i++) {
    Variable _and = ctx.create();
    c[i + 1] = ctx.create();
    addAnd(_and, c[i], p[i]);
    addOr(c[i + 1], c[i], g[i]);
  }

  // result[i] = p[i] ^ c[i]
  for (int i = 0; i < 32; i++) {
    result[i] = ctx.create();
    addXor(result[i], p[i], c[i]);
  }

  return result;
}

void BvSolver::blastEq(const Bitvector &a, const Bitvector &b) {
  for (int i = 0; i < 32; i++)
    uf.link(a[i], b[i]);
}

Bitvector BvSolver::blastOp(BvExpr *expr) {
  switch (expr->ty) {
  case BvExpr::Add: {
    auto l = blastOp(expr->l);
    auto r = blastOp(expr->r);
    return blastAdd(l, r);
  }
  default:
    assert(false);
  }
}

void BvSolver::blast(BvExpr *expr) {
  switch (expr->ty) {
  case BvExpr::Eq: {
    auto l = blastOp(expr->l);
    auto r = blastOp(expr->r);
    blastEq(l, r);
    break;
  }
  default:
    assert(false);
  }
}

bool BvSolver::infer(BvExpr *expr) {
  blast(expr);

  // Unify equality.
  for (auto &clause : reserved) {
    for (auto &x : clause) {
      if (uf.has(x))
        x = uf.find(x);
    }
  }

  // Add all clauses to the solver.
  for (const auto &clause : reserved)
    solver.addClause(clause);

  solver.init(/*varcnt=*/ctx.getTotal());
  std::vector<signed char> assignments;
  bool succ = solver.solve(assignments);
  return succ;
}
