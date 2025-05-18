#ifndef SMT_H
#define SMT_H

#include "BvExpr.h"
#include "CDCL.h"
#include <vector>
#include <unordered_map>

namespace smt {

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
  UnionFind uf;
  std::vector<Clause> reserved;

  // These are literals false and true in SAT solver.
  Variable _false;
  Variable _true;

  void reserve(const Clause &clause) { reserved.push_back(clause); }

  // This means that `o` is `a op b`.
  void addAnd(Variable o, Variable a, Variable b);
  void addOr (Variable o, Variable a, Variable b);
  void addXor(Variable o, Variable a, Variable b);

  // These blast functions will add clauses to solver.
  Bitvector blastAdd(const Bitvector &a, const Bitvector &b);
  Bitvector blastMul(const Bitvector &a, const Bitvector &b);
  void blastEq(const Bitvector &a, const Bitvector &b);

  // Blast operators that have a value.
  Bitvector blastOp(BvExpr *expr);
  // Blast operators that don't have a value. This means it's top-level.
  void blast(BvExpr *expr);

  void simplify(BvExpr *expr);
public:
  BvSolver();

  bool infer(BvExpr *expr);
  std::unordered_map<BvExpr*, int64_t> model();
};

}

#endif
