#ifndef SMT_H
#define SMT_H

#include "BvExpr.h"
#include "CDCL.h"
#include "../../main/Options.h"
#include <vector>
#include <unordered_map>

namespace smt {

// Bitvector[0] is the least significant bit.
using Bitvector = std::vector<Variable>;

class BvSolver {
  using Clause = std::vector<Atomic>;

  SATContext ctx;
  Solver solver;
  std::unordered_map<std::string, Bitvector> bindings;
  std::unordered_map<BvExpr*, Bitvector> cache;
  std::vector<Clause> reserved;
  std::vector<signed char> assignments;

  // These are literals false and true in SAT solver.
  Variable _false;
  Variable _true;

  void reserve(const Clause &clause) { reserved.push_back(clause); }

  // This means that `o` is `a op b`.
  void addAnd(Variable out, Variable a, Variable b);
  void addOr (Variable out, Variable a, Variable b);
  void addXor(Variable out, Variable a, Variable b);
  void addNot(Variable out, Variable a);

  // Combined operations.

  // a & !b
  void addAndNot(Variable out, Variable a, Variable b);
  // !(a ^ b)
  void addXnor  (Variable out, Variable a, Variable b);

  // These blast functions will add clauses to solver.
  Bitvector blastConst(int vi);
  Bitvector blastVar(const std::string &name);

  // Add 32-bit numbers.
  Bitvector blastAdd(const Bitvector &a, const Bitvector &b, bool withCin = false);

  // Subtract with borrow bit `c[n]`.
  Bitvector blastSubBorrowed(const Bitvector &a, const Bitvector &b, Variable borrow);

  // Bitwise operations.
  Bitvector blastAnd(const Bitvector &a, const Bitvector &b);
  Bitvector blastOr(const Bitvector &a, const Bitvector &b);
  Bitvector blastXor(const Bitvector &a, const Bitvector &b);
  Bitvector blastNot(const Bitvector &a);
  
  // Left shift by constant.
  Bitvector blastLsh(const Bitvector &a, int x);

  // Absolute value.
  Bitvector blastAbs(const Bitvector &a);
  // Minus.
  Bitvector blastMinus(const Bitvector &a);
  
  // This gives a 64-bit long vector.
  Bitvector blastFullMul(const Bitvector &a, const Bitvector &b);
  // This gives a 64-bit long vector, and performs signed multiplication.
  Bitvector blastFullSMul(const Bitvector &a, const Bitvector &b);
  // Multiplies 64-bit vectors and get lower 64-bits.
  Bitvector blastFullLMul(const Bitvector &a, const Bitvector &b);
  // Multiplies 64-bit vectors and get lower 64-bits, and performs signed multiplication.
  Bitvector blastFullSLMul(const Bitvector &a, const Bitvector &b);

  // Unsigned division.
  Bitvector blastDiv(const Bitvector &a, const Bitvector &b);
  // Signed division.
  Bitvector blastSDiv(const Bitvector &a, const Bitvector &b);

  // This gives a full multiplication and then modulus constant x.
  // When `x` is zero, this modulus is 2^32, i.e. take the least significant 32 bits.
  Bitvector blastMulMod(const Bitvector &a, const Bitvector &b, int x);

  // If-then-else.
  Bitvector blastIte(Variable c, const Bitvector &a, const Bitvector &b);

  void blastEq(const Bitvector &a, const Bitvector &b);
  void blastNe(const Bitvector &a, const Bitvector &b);

  // Blast operators that have a value.
  Bitvector blastOp(BvExpr *expr);
  // Blast operators that don't have a value. This means it's top-level.
  void blast(BvExpr *expr);
  // Blast boolean-valued operators.
  Variable blastCond(BvExpr *expr);

  int eval(BvExpr *expr);
public:
  using Model = std::unordered_map<std::string, int>;

  BvSolver();
  BvSolver(const sys::Options &opts);
  sys::Options opts;

  bool infer(BvExpr *expr);
  int extract(const std::string &name);
  bool has(const std::string &name) { return bindings.count(name); }
  int eval(BvExpr *expr, const std::unordered_map<std::string, int> &external);
  void assign(const std::string &name, int value);
  void unassign() { bindings.clear(); }
  Model model();
};

BvExpr *simplify(BvExpr *expr, BvExprContext &ctx);

}

#endif
