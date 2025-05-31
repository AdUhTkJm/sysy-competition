#ifndef CLAUSE_H
#define CLAUSE_H

#include <iostream>
#include <vector>
#include <unordered_map>
#include <string>
#include <queue>

namespace smt {

// Atomic propositions.
// Encoding: 2x means the variable `x`, 2x+1 means the negation of it.
// We need to index vectors with literals, so we can't use the obvious `-x`.
using Variable = int;
using Atomic = int;

// A clause is a logical disjuntion of formulae.
struct Clause {
  std::vector<Atomic> content;
  Atomic watch[2];
  void dump(std::ostream &os = std::cerr) const;
};

// https://www.cs.princeton.edu/~zkincaid/courses/fall18/readings/SATHandbook-CDCL.pdf
// See this book for CDCL.
class Solver {
  using Boolean = signed char;
  constexpr static Boolean False = -1;
  constexpr static Boolean True = 1;
  constexpr static Boolean Unassigned = 0;

  std::vector<Clause*> clauses;

  // We use -1 for false, 1 for true, 0 for unassigned. Note the difference from bools in C++.
  // Also note std::vector<bool> is not a vector of bools.
  std::vector<Boolean> assignment;
  std::vector<Clause*> antecedent;
  std::vector<int> decisionLevel;

  // All assigned variable in order. It includes decision variables and variables updated by implication.
  std::vector<Atomic> trail;
  // trailLim[level] contains the index `i` of `trail` s.t. forall j >= i, decisionLevel[trail[j]] >= level.
  // In human-readable words, trail_lim[level] = index in trail where that level begins.
  std::vector<int> trailLim;

  // Every clause watches 2 literals. See Chaff's paper:
  // https://www.princeton.edu/~chaff/publication/DAC2001v56.pdf
  // Here watched[i] contains all clauses that watch on literal `i`.
  //
  // The basic idea is, in unit propagation, we only care about the clauses (of size N) that have N-1 falses;
  // In other words, we would only want to be notified when an assignment might cause the false-count goes
  // from N-2 to N-1.
  // To approximately do this, we can pick 2 non-false literals at each clause. We don't need to care about any
  // updates to the clause if they do not touch those literals; only when one of them gets assigned to false,
  // it would become possible that the clause now have N-1 falses.
  std::vector<std::vector<Clause*>> watched;

  // Current decision level.
  int dl = 0;
  // Total variable count.
  int varcnt = -1;
  // Conflict count since last decay.
  int conflict = 0;
  // The current place we should start dealing with unit propagation.
  size_t qhead = 0;

  // Set to true when `addClause` detects a conflict.
  bool addedConflict = false;
  // Set to true when unit propagation detects a conflict.
  bool unitConflict = false;

  Clause *conflictClause;

  // Activity scores for each variable.
  std::vector<double> activity;
  // Heap of variables.
  std::priority_queue<std::pair<double, Variable>> vheap;
  // The phase (the preferred value of exploration) of variables.
  std::vector<Boolean> phase;

  // Increment of activity.
  double inc = 1.0;
  // Decay.
  static constexpr double decay = 0.95;

  Boolean value(Atomic atom);

  void unitPropagation();
  void backtrack(int level);

  // Find the only unassigned variable within a clause.
  Atomic findUnit(const std::vector<Atomic> &unit);

  bool allAssigned();

  // Returns the backtrack level and learnt clause.
  std::pair<int, std::vector<Atomic>> analyzeConflict();
  // Pick up a variable and a value to assign.
  std::pair<Variable, Boolean> pickPivot();

  // Push `atom` to `trail` for further unit propagation, where `atom` must be true.
  void enqueue(Atomic atom, Clause *antecedent);

  Clause *addLearnt(std::vector<Atomic> clause);
public:
  void dump(std::ostream &os = std::cerr) const;

  // Add external clause. Does extra tidying.
  Clause *addClause(const std::vector<Atomic> &clause);
  // Returns true if satisfiable.
  bool solve(std::vector<signed char> &assignments);

  void init(int varcnt);
};

class SATContext {
  int total = 0;
public:
  int getTotal() { return total; }

  Variable create() { return total++; }
  Atomic neg(Variable x) { return (x << 1) + 1; }
  Atomic pos(Variable x) { return (x << 1); }
  void reset() { total = 0; }
};

inline std::ostream &operator<<(std::ostream &os, const Clause *clause) {
  clause->dump(os);
  return os;
}

inline std::ostream &operator<<(std::ostream &os, const Solver &solver) {
  solver.dump(os);
  return os;
}

}

#endif
