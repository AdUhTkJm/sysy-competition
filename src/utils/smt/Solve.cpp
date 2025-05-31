#include "SMT.h"
#include <cassert>
#include <chrono>

using namespace smt;

BvSolver::BvSolver(const sys::Options &opts): opts(opts) {
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
    addOr(c[i + 1], _and, g[i]);
  }

  // result[i] = p[i] ^ c[i]
  for (int i = 0; i < 32; i++) {
    result[i] = ctx.create();
    addXor(result[i], p[i], c[i]);
  }

  return result;
}

Bitvector BvSolver::blastAddL(const Bitvector &a, const Bitvector &b) {
  // Still a CLA adder.
  // Just that the length is changed into 64.
  Bitvector g(64), p(64), result(64);

  for (int i = 0; i < 64; i++) {
    g[i] = ctx.create();
    p[i] = ctx.create();
    addAnd(g[i], a[i], b[i]);
    addXor(p[i], a[i], b[i]);
  }

  // According to the meaning described above,
  // Carry c[i+1] = g[i] | (c[i] & p[i])
  Bitvector c(64);
  c[0] = _false;
  for (int i = 0; i < 63; i++) {
    Variable _and = ctx.create();
    c[i + 1] = ctx.create();
    addAnd(_and, c[i], p[i]);
    addOr(c[i + 1], _and, g[i]);
  }

  // result[i] = p[i] ^ c[i]
  for (int i = 0; i < 64; i++) {
    result[i] = ctx.create();
    addXor(result[i], p[i], c[i]);
  }

  return result;
}

Bitvector BvSolver::blastConst(int vi) {
  Bitvector c(32);
  for (int i = 0; i < 32; i++)
    c[i] = (vi >> i) & 1 ? _true : _false;
  return c;
}

Bitvector BvSolver::blastVar(const std::string &name) {
  if (bindings.count(name))
    return bindings[name];

  Bitvector c(32);
  for (int i = 0; i < 32; i++)
    c[i] = ctx.create();
  bindings[name] = c;
  return c;
}

Bitvector BvSolver::blastLsh(const Bitvector &a, int x) {
  Bitvector c(32); 
  // Lower x bits are zero.
  for (int i = 0; i < x; i++)
    c[i] = _false;
  // Upper 32-x bits are shifted from x.
  for (int i = x; i < 32; i++)
    c[i] = a[i - x];

  return c;
}

// Simply shift-and-add.
Bitvector BvSolver::blastFullMul(const Bitvector &a, const Bitvector &b) {
  Bitvector c(64);  
  for (int i = 0; i < 32; i++) {
    // Represent (a << i).
    Bitvector s(64, _false);
    for (int j = 0; j < 32; j++)
      s[i + j] = a[j];

    Bitvector masked(64, _false);
    for (int j = i; j < i + 32; j++) {
      masked[j] = ctx.create();
      addAnd(masked[j], s[j], b[i]);
    }

    c = blastAddL(c, masked);
  }
  return c;
}

Bitvector BvSolver::blastMulMod(const Bitvector &a, const Bitvector &b, int x) {
  Bitvector c = blastFullMul(a, b);
  if (x == 0) {
    c.resize(32);
    return c;
  }
  std::cerr << "NYI\n";
  assert(false);
}

void BvSolver::blastEq(const Bitvector &a, const Bitvector &b) {
  for (int i = 0; i < 32; i++) {
    reserve({ ctx.pos(a[i]), ctx.neg(b[i]) });
    reserve({ ctx.neg(a[i]), ctx.pos(b[i]) });
  }
}

// Implemented as (a[1] ^ b[1]) | (a[2] ^ b[2]) | ...
void BvSolver::blastNe(const Bitvector &a, const Bitvector &b) {
  Bitvector c(32);
  for (int i = 0; i < 32; i++) {
    c[i] = ctx.create();
    addXor(c[i], a[i], b[i]);
  }
  
  for (int i = 0; i < 32; i++)
    c[i] = ctx.pos(c[i]);
  reserve(c);
}

Bitvector BvSolver::blastOp(BvExpr *expr) {
  switch (expr->ty) {
  case BvExpr::Add: {
    auto l = blastOp(expr->l);
    auto r = blastOp(expr->r);
    return blastAdd(l, r);
  }
  case BvExpr::Mul: {
    auto l = blastOp(expr->l);
    auto r = blastOp(expr->r);
    return blastMulMod(l, r, 0);
  }
  case BvExpr::Const:
    return blastConst(expr->vi);
  case BvExpr::Var:
    return blastVar(expr->name);
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
  case BvExpr::Ne: {
    auto l = blastOp(expr->l);
    auto r = blastOp(expr->r);
    blastNe(l, r);
    break;
  }
  default:
    assert(false);
  }
}

bool BvSolver::infer(BvExpr *expr) {
  using namespace std::chrono;

  blast(expr);

  // Add all clauses to the solver.
  solver.init(/*varcnt=*/ctx.getTotal());
  decltype(high_resolution_clock::now()) starttime;

  if (opts.verbose) {
    for (const auto &clause : reserved) {
      for (auto atom : clause) {
        int v = (atom >> 1) + 1;
        std::cerr << (atom & 1 ? -v : v) << " ";
      }
      std::cerr << "0\n";
    }
  }

  if (opts.stats || opts.verbose) {
    std::cerr << "variables: " << ctx.getTotal() << "\n";
    std::cerr << "clauses: " << reserved.size() << "\n";
    starttime = high_resolution_clock::now();
  }

  for (const auto &clause : reserved)
    solver.addClause(clause);

  bool succ = solver.solve(assignments);
  
  if (opts.stats || opts.verbose) {
    auto endtime = high_resolution_clock::now();
    duration<double, std::micro> duration = endtime - starttime;
    std::cerr << "elapsed: " << duration.count() << " us\n";
  }
  
  return succ;
}

int BvSolver::extract(const std::string &name) {
  if (!bindings.count(name)) {
    std::cerr << "unbounded name: " << name << "\n";
    assert(false);
  }

  Bitvector bv = bindings[name];
  unsigned result = 0;
  for (int i = 0; i < 32; i++)
    result |= (int(assignments[bv[i]]) << i);
  return int(result);
}
