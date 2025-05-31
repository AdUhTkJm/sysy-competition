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

class UnionFind {
  std::unordered_map<Variable, int> parent;
  std::unordered_map<Variable, int> rank;

public:
  bool has(Variable x);
  Variable find(Variable x);
  void link(Variable x, Variable y);
  bool equiv(Variable x, Variable y);
};

class BvSolver {
  using Clause = std::vector<Atomic>;

  SATContext ctx;
  Solver solver;
  sys::Options opts;
  std::unordered_map<std::string, Bitvector> bindings;
  std::vector<Clause> reserved;
  std::vector<signed char> assignments;

  // These are literals false and true in SAT solver.
  Variable _false;
  Variable _true;

  void reserve(const Clause &clause) { reserved.push_back(clause); }

  // This means that `o` is `a op b`.
  void addAnd(Variable o, Variable a, Variable b);
  void addOr (Variable o, Variable a, Variable b);
  void addXor(Variable o, Variable a, Variable b);

  // These blast functions will add clauses to solver.
  Bitvector blastConst(int vi);
  Bitvector blastVar(const std::string &name);

  // Add 32-bit numbers.
  Bitvector blastAdd(const Bitvector &a, const Bitvector &b);
  // Add 64-bit numbers. Only used internally.
  Bitvector blastAddL(const Bitvector &a, const Bitvector &b);
  
  // Left shift by constant.
  Bitvector blastLsh(const Bitvector &a, int x);
  
  // This gives a length-64 bit vector.
  Bitvector blastFullMul(const Bitvector &a, const Bitvector &b);
  // This gives a full multiplication and then modulus constant x.
  // When `x` is zero, this modulus is 2^32, i.e. take the least significant 32 bits.
  Bitvector blastMulMod(const Bitvector &a, const Bitvector &b, int x);

  void blastEq(const Bitvector &a, const Bitvector &b);
  void blastNe(const Bitvector &a, const Bitvector &b);

  // Blast operators that have a value.
  Bitvector blastOp(BvExpr *expr);
  // Blast operators that don't have a value. This means it's top-level.
  void blast(BvExpr *expr);

  void simplify(BvExpr *expr);
public:
  BvSolver(const sys::Options &opts);

  bool infer(BvExpr *expr);
  int extract(const std::string &name);
  std::unordered_map<BvExpr*, int64_t> model();
};

}

#endif
