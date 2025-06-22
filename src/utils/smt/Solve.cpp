#include "SMT.h"
#include <cassert>
#include <chrono>

using namespace smt;

void BvExpr::dump(std::ostream &os) {
  if (ty == BvExpr::Const) {
    os << vi;
    return;
  }
  if (ty == BvExpr::Var) {
    os << name;
    return;
  }

  std::string x = names[ty];
  x[0] = tolower(x[0]);
  os << "(" << x;
  if (cond)
    os << " " << cond;
  if (l)
    os << " " << l;
  if (r)
    os << " " << r;
  os << ")";
}

BvSolver::BvSolver(): BvSolver(sys::Options {}) {}

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
  reserve({ ctx.neg(a), ctx.neg(b), ctx.pos(out) });
  reserve({ ctx.pos(a), ctx.neg(out) });
  reserve({ ctx.pos(b), ctx.neg(out) });
}

void BvSolver::addOr(Variable out, Variable a, Variable b) {
  reserve({ ctx.pos(a), ctx.pos(b), ctx.neg(out) });
  reserve({ ctx.neg(a), ctx.pos(out) });
  reserve({ ctx.neg(b), ctx.pos(out) });
}

void BvSolver::addXor(Variable out, Variable a, Variable b) {
  reserve({ ctx.neg(a), ctx.neg(b), ctx.neg(out) });
  reserve({ ctx.pos(a), ctx.pos(b), ctx.neg(out) });
  reserve({ ctx.pos(a), ctx.neg(b), ctx.pos(out) });
  reserve({ ctx.neg(a), ctx.pos(b), ctx.pos(out) });
}

void BvSolver::addNot(Variable out, Variable a) {
  reserve({ ctx.neg(a), ctx.neg(out) });
  reserve({ ctx.pos(a), ctx.pos(out) });
}

// Equivalent to (a & !b).
// Just change the polarity of `b` in addAnd.
void BvSolver::addAndNot(Variable out, Variable a, Variable b) {
  reserve({ ctx.neg(a), ctx.pos(b), ctx.pos(out) });
  reserve({ ctx.pos(a), ctx.neg(out) });
  reserve({ ctx.neg(b), ctx.neg(out) });
}

void BvSolver::addXnor(Variable out, Variable a, Variable b) {
  reserve({ ctx.neg(out), ctx.pos(a), ctx.neg(b) });
  reserve({ ctx.neg(out), ctx.neg(a), ctx.pos(b) });
  reserve({ ctx.pos(out), ctx.pos(a), ctx.pos(b) });
  reserve({ ctx.pos(out), ctx.neg(a), ctx.neg(b) });
}

// Both serial (ripple-carry) adders and CLA adders need 2 and, 1 or and 2 xors.
// So I'll just choose the CLA one.
Bitvector BvSolver::blastAdd(const Bitvector &a, const Bitvector &b, bool withCin) {
  // Consider a CLA adder.
  //   g[i] = a[i] & b[i]: "Generate", means to generate a carry;
  //   p[i] = a[i] ^ b[i]: "Propagate", means the carry from prev. bit will propagate through.
  // Refer to Cambridge Part IA, Digital Electronics.
  // (Perhaps I need some revision.)
  int n = a.size();
  Bitvector g(n), p(n), result(n);

  for (int i = 0; i < n; i++) {
    g[i] = ctx.create();
    p[i] = ctx.create();
    addAnd(g[i], a[i], b[i]);
    addXor(p[i], a[i], b[i]);
  }

  // According to the meaning described above,
  // Carry c[i+1] = g[i] | (c[i] & p[i])
  Bitvector c(n);
  c[0] = withCin ? _true : _false;
  for (int i = 0; i < n - 1; i++) {
    Variable _and = ctx.create();
    c[i + 1] = ctx.create();
    addAnd(_and, c[i], p[i]);
    addOr(c[i + 1], _and, g[i]);
  }

  // result[i] = p[i] ^ c[i]
  for (int i = 0; i < n; i++) {
    result[i] = ctx.create();
    addXor(result[i], p[i], c[i]);
  }

  return result;
}

Bitvector BvSolver::blastAnd(const Bitvector &a, const Bitvector &b) {
  Bitvector c(32);
  for (int i = 0; i < 32; i++) {
    c[i] = ctx.create();
    addAnd(c[i], a[i], b[i]);
  }
  return c;
}

Bitvector BvSolver::blastOr(const Bitvector &a, const Bitvector &b) {
  int n = a.size();
  Bitvector c(n);
  for (int i = 0; i < n; i++) {
    c[i] = ctx.create();
    addOr(c[i], a[i], b[i]);
  }
  return c;
}

Bitvector BvSolver::blastXor(const Bitvector &a, const Bitvector &b) {
  int n = a.size();
  Bitvector c(n);
  for (int i = 0; i < n; i++) {
    c[i] = ctx.create();
    addXor(c[i], a[i], b[i]);
  }
  return c;
}

Bitvector BvSolver::blastNot(const Bitvector &a) {
  int n = a.size();
  Bitvector c(n);
  for (int i = 0; i < n; i++) {
    c[i] = ctx.create();
    addNot(c[i], a[i]);
  }
  return c;
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

Bitvector BvSolver::blastFullMul(const Bitvector &a, const Bitvector &b) {
  assert(a.size() == 32 && b.size() == 32);
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

    c = blastAdd(c, masked);
  }
  return c;
}

Bitvector BvSolver::blastFullLMul(const Bitvector &a, const Bitvector &b) {
  assert(a.size() == 64 && b.size() == 64);
  Bitvector c(64);

  for (int i = 0; i < 64; i++) {
    Bitvector s(64, _false);
    for (int j = 0; j < 64 - i; j++)
      s[i + j] = a[j];

    Bitvector masked(64, _false);
    for (int j = i; j < 64; j++) {
      masked[j] = ctx.create();
      addAnd(masked[j], s[j], b[i]);
    }

    c = blastAdd(c, masked);
  }

  return c;
}

Bitvector BvSolver::blastFullSMul(const Bitvector &a, const Bitvector &b) {
  auto aabs = blastAbs(a);
  auto babs = blastAbs(b);

  // Sign bits.
  auto sa = a.back(), sb = b.back();
  // Whether the final result is negative.
  auto neg = ctx.create();
  addXor(neg, sa, sb);

  auto umul = blastFullMul(aabs, babs);
  return blastIte(neg, blastMinus(umul), umul);
}

Bitvector BvSolver::blastFullSLMul(const Bitvector &a, const Bitvector &b) {
  auto aabs = blastAbs(a);
  auto babs = blastAbs(b);

  // Sign bits.
  auto sa = a.back(), sb = b.back();
  // Whether the final result is negative.
  auto neg = ctx.create();
  addXor(neg, sa, sb);

  auto umul = blastFullLMul(aabs, babs);
  return blastIte(neg, blastMinus(umul), umul);
}

Bitvector BvSolver::blastSubBorrowed(const Bitvector &a, const Bitvector &b, Variable borrow) {
  assert(a.size() == b.size());
  // Now this can adapt to both 32 and 64 bit.
  int n = a.size();
  Bitvector nb(n);
  for (int i = 0; i < n; i++) {
    nb[i] = ctx.create();
    addNot(nb[i], b[i]);
  }

  // Exact the full adder in `blastAdd`.
  Bitvector result(n), g(n), p(n), c(n + 1);
  for (int i = 0; i < n; ++i) {
    g[i] = ctx.create();
    p[i] = ctx.create();

    addAnd(g[i], a[i], nb[i]);
    addXor(p[i], a[i], nb[i]);
  }

  c[0] = _true;
  for (int i = 0; i < n; ++i) {
    Variable tmp = ctx.create();
    c[i + 1] = ctx.create();
    addAnd(tmp, c[i], p[i]);
    addOr(c[i + 1], tmp, g[i]);
  }
  
  for (int i = 0; i < n; i++) {
    result[i] = ctx.create();
    addXor(result[i], p[i], c[i]);
  }

  addNot(borrow, c[n]);
  return result;
}

Bitvector BvSolver::blastDiv(const Bitvector &a, const Bitvector &_b) {
  int n = a.size();
  assert(n >= _b.size());

  // Sign-extend `b`.
  Bitvector b(n);
  std::copy(_b.begin(), _b.end(), b.begin());
  for (int i = _b.size(); i < n; i++)
    b[i] = _b.back();

  Bitvector quotient(n);
  Bitvector remainder(n, _false);

  for (int i = n - 1; i >= 0; i--) {
    for (int j = n - 1; j > 0; j--)
      remainder[j] = remainder[j - 1];
    remainder[0] = a[i];

    Variable borrow = ctx.create();
    Bitvector tmpRem = blastSubBorrowed(remainder, b, borrow);

    quotient[i] = ctx.create();
    addNot(quotient[i], borrow);

    for (int j = 0; j < n; ++j) {
      auto tmp = ctx.create();
      auto _and = ctx.create();
      auto _and2 = ctx.create();

      addAnd(_and, remainder[j], borrow);
      addAndNot(_and2, tmpRem[j], borrow);
      addOr(tmp, _and, _and2);
      remainder[j] = tmp;
    }
  }

  return quotient;
}

Bitvector BvSolver::blastSDiv(const Bitvector &a, const Bitvector &b) {
  auto aabs = blastAbs(a);
  auto babs = blastAbs(b);

  // Sign bits.
  auto sa = a.back(), sb = b.back();
  // Whether the final result is negative.
  auto neg = ctx.create();
  addXor(neg, sa, sb);

  auto udiv = blastDiv(aabs, babs);
  return blastIte(neg, blastMinus(udiv), udiv);
}

// -a = ~a+1
Bitvector BvSolver::blastMinus(const Bitvector &a) {
  Bitvector zero(a.size(), _false);
  auto _not = blastNot(a);
  return blastAdd(_not, zero, /*withCin=*/ true);
}

Bitvector BvSolver::blastAbs(const Bitvector &a) {
  return blastIte(a.back(), blastMinus(a), a);
}

Bitvector BvSolver::blastIte(Variable c, const Bitvector &a, const Bitvector &b) {
  int n = a.size();
  Bitvector d(n);
  for (int i = 0; i < n; i++) {
    d[i] = ctx.create();
    auto _and = ctx.create();
    auto _and2 = ctx.create();

    addAnd(_and, a[i], c);
    addAndNot(_and2, b[i], c);
    addOr(d[i], _and, _and2);
  }
  return d;
}

Bitvector BvSolver::blastMulMod(const Bitvector &a, const Bitvector &b, int x) {
  Bitvector c = blastFullSMul(a, b);
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

Variable BvSolver::blastCond(BvExpr *expr) {
  switch (expr->ty) {
  case BvExpr::Const:
    return expr->vi ? _true : _false;
  case BvExpr::And: {
    auto l = blastCond(expr->l);
    auto r = blastCond(expr->r);
    // Const fold.
    if (l == _true)
      return r;
    if (l == _false)
      return _false;
    
    auto _and = ctx.create();
    addAnd(_and, l, r);
    return _and;
  }
  case BvExpr::Or: {
    auto l = blastCond(expr->l);
    auto r = blastCond(expr->r);
    // Const fold.
    if (l == _false)
      return r;
    if (l == _true)
      return _true;
    
    auto _or = ctx.create();
    addOr(_or, l, r);
    return _or;
  }
  case BvExpr::Not: {
    auto l = blastCond(expr->l);
    // Const fold.
    if (l == _true)
      return _false;
    if (l == _false)
      return _true;

    auto _not = ctx.create();
    addNot(_not, l);
    return _not;
  }
  case BvExpr::Eq: {
    auto l = blastOp(expr->l);
    auto r = blastOp(expr->r);

    Variable c = ctx.create();
    addXnor(c, l[0], r[0]);

    for (int i = 1; i < 32; ++i) {
      Variable _xnor = ctx.create();
      Variable _and = ctx.create();
      addXnor(_xnor, l[i], r[i]);
      addAnd(_and, c, _xnor);
      c = _and;
    }

    return c;
  }
  // Simply flip the signs of Eq above.
  case BvExpr::Ne: {
    auto l = blastOp(expr->l);
    auto r = blastOp(expr->r);

    Variable c = _false;

    for (int i = 0; i < 32; ++i) {
      Variable _xor = ctx.create();
      Variable _or = ctx.create();
      addXor(_xor, l[i], r[i]);
      addOr(_or, c, _xor);
      c = _or;
    }

    return c;
  }
  case BvExpr::Lt: {
    auto l = blastOp(expr->l);
    auto r = blastOp(expr->r);
    auto diff = blastAdd(l, blastNot(r), true);
    // l - r < 0
    return diff[31];
  }
  case BvExpr::Le: {
    auto l = blastOp(expr->l);
    auto r = blastOp(expr->r);
    auto diff = blastAdd(l, blastNot(r), false);
    // l + ~r < 0 (i.e. l + (-1 - r) < 0)
    return diff[31];
  }
  default: {
    // OR everything together.
    auto bv = blastOp(expr);
    assert(bv.size() == 32);
    Variable c = _false;
    for (int i = 0; i < 32; i++) {
      Variable tmp = ctx.create();
      addOr(tmp, c, bv[i]);
      c = tmp;
    }
    return c;
  }
  }
}

Bitvector BvSolver::blastOp(BvExpr *expr) {
  if (cache.count(expr))
    return cache[expr];

  switch (expr->ty) {
  case BvExpr::Add: {
    auto l = blastOp(expr->l);
    auto r = blastOp(expr->r);
    return cache[expr] = blastAdd(l, r);
  }
  case BvExpr::Sub: {
    auto l = blastOp(expr->l);
    auto r = blastOp(expr->r);
    return cache[expr] = blastAdd(l, blastNot(r), /*withCin=*/ true);
  }
  case BvExpr::Mul: {
    auto l = blastOp(expr->l);
    auto r = blastOp(expr->r);
    return cache[expr] = blastMulMod(l, r, 0);
  }
  case BvExpr::Div: {
    auto l = blastOp(expr->l);
    auto r = blastOp(expr->r);
    return cache[expr] = blastSDiv(l, r);
  }
  case BvExpr::Mod: {
    auto l = blastOp(expr->l);
    auto r = blastOp(expr->r);
    auto div = blastSDiv(l, r);
    auto divmul = blastMulMod(div, r, 0);
    return cache[expr] = blastAdd(l, blastNot(divmul), /*withCin=*/ true);
  }
  case BvExpr::MulMod: {
    auto l = blastOp(expr->l);
    auto r = blastOp(expr->r);
    auto cond = blastOp(expr->cond);
    // (cond * l) % r
    auto mul = blastFullSMul(cond, l);
    auto div = blastSDiv(mul, r);

    // Sign-extend `r` to 64 bit.
    r.resize(64);
    for (int i = 32; i < 64; i++)
      r[i] = r[31];

    auto divmul = blastFullSLMul(div, r);
    auto sub = blastAdd(mul, blastNot(divmul), /*withCin=*/ true);
    sub.resize(32);
    return cache[expr] = sub;
  }
  case BvExpr::And: {
    auto l = blastOp(expr->l);
    auto r = blastOp(expr->r);
    return cache[expr] = blastAnd(l, r);
  }
  case BvExpr::Or: {
    auto l = blastOp(expr->l);
    auto r = blastOp(expr->r);
    return cache[expr] = blastOr(l, r);
  }
  case BvExpr::Xor: {
    auto l = blastOp(expr->l);
    auto r = blastOp(expr->r);
    return cache[expr] = blastXor(l, r);
  }
  case BvExpr::Ite: {
    auto c = blastCond(expr->cond);
    auto l = blastOp(expr->l);
    auto r = blastOp(expr->r);
    return cache[expr] = blastIte(c, l, r);
  }
  case BvExpr::Abs: {
    auto l = blastOp(expr->l);
    return cache[expr] = blastAbs(l);
  }
  case BvExpr::Minus: {
    auto l = blastOp(expr->l);
    return cache[expr] = blastMinus(l);
  }
  case BvExpr::Lsh: {
    auto l = blastOp(expr->l);
    if (expr->r->ty == BvExpr::Const) {
      int n = l.size();
      Bitvector c(n);
      int vi = expr->r->vi;
      for (int i = vi; i < c.size(); i++)
        c[i] = l[i - vi];
      return cache[expr] = c;
    };
    assert(false && "lsh nyi");
  }
  case BvExpr::Extr: {
    // Zero-extend.
    auto l = blastOp(expr->l);
    int n = l.size();
    Bitvector c(n, _false);
    c[0] = l[expr->vi];
    return cache[expr] = c;
  }
  case BvExpr::Const:
    return cache[expr] = blastConst(expr->vi);
  case BvExpr::Var:
    return cache[expr] = blastVar(expr->name);
  case BvExpr::Eq:
  case BvExpr::Ne:
  case BvExpr::Not:
  case BvExpr::Lt:
  case BvExpr::Le: {
    // Extend it to 32-bit.
    Variable d = blastCond(expr);
    Bitvector c(32, _false);
    c[0] = d;
    return cache[expr] = c;
  }
  default:
    std::cerr << "unknown op: " << expr << "\n";
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
  case BvExpr::And: {
    auto l = blastCond(expr->l);
    auto r = blastCond(expr->r);
    reserve({ ctx.pos(l) });
    reserve({ ctx.pos(r) });
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

void BvSolver::assign(const std::string &name, int value) {
  if (bindings.count(name)) {
    std::cerr << "already assigned: " << name << "\n";
    assert(false);
  }

  bindings[name] = blastConst(value);
}

int BvSolver::eval(BvExpr *expr, const std::unordered_map<std::string, int> &external) {
  for (auto &[k, v] : external) {
    Bitvector c(32);
    for (int i = 0; i < 32; i++) {
      c[i] = assignments.size();
      assignments.push_back((v >> i) & 1);
    }
    bindings[k] = c;
  }
  return eval(expr);
}

int BvSolver::eval(BvExpr *expr) {
  switch (expr->ty) {
  case BvExpr::Const:
    return expr->vi;
  case BvExpr::Var:
    return extract(expr->name);
  case BvExpr::Add:
    return eval(expr->l) + eval(expr->r);
  case BvExpr::Sub:
    return eval(expr->l) - eval(expr->r);
  case BvExpr::Mul:
    return eval(expr->l) * eval(expr->r);
  case BvExpr::Div:
    return eval(expr->l) / eval(expr->r);
  case BvExpr::Mod:
    return eval(expr->l) % eval(expr->r);
  case BvExpr::Lsh:
    return eval(expr->l) << eval(expr->r);
  case BvExpr::Rsh:
    return eval(expr->l) >> eval(expr->r);
  case BvExpr::Lt:
    return eval(expr->l) < eval(expr->r);
  case BvExpr::Le:
    return eval(expr->l) <= eval(expr->r);
  case BvExpr::MulMod:
    return (((int64_t) eval(expr->cond)) * eval(expr->l)) % eval(expr->r);
  case BvExpr::Ite:
    if (eval(expr->cond))
      return eval(expr->l);
    return eval(expr->r);
  case BvExpr::And:
    return eval(expr->l) & eval(expr->r);
  case BvExpr::Or:
    return eval(expr->l) | eval(expr->r);
  case BvExpr::Xor:
    return eval(expr->l) ^ eval(expr->r);
  case BvExpr::Not:
    return !eval(expr->l);
  case BvExpr::Eq:
    return eval(expr->l) == eval(expr->r);
  case BvExpr::Ne:
    return eval(expr->l) != eval(expr->r);
  case BvExpr::Extr:
    return (unsigned) eval(expr->l) & (1u << expr->vi);
  default:
    std::cerr << "unsupported type " << BvExpr::names[expr->ty] << "\n";
    assert(false);
  }
}

BvSolver::Model BvSolver::model() {
  Model result;
  for (const auto &[name, _] : bindings)
    result[name] = extract(name);
  return result;
}
