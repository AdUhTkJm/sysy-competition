#ifndef CLAUSE_H
#define CLAUSE_H

#include <vector>

namespace smt {

// Atomic propositions.
using Atomic = int;

// A clause is a logical disjuntion of formulae.
struct Clause {
  std::vector<Atomic> content;
};

// For passes outside.
class FormulaAST {
public:
  enum Type {
    And, Or, Not, Xor, Implies, Equiv, Atom
  } type;

  FormulaAST *l, *r;
  Atomic atom;
};

// A formula in propositional logic.
class Formula {
  std::vector<Clause> clauses;
public:
  Formula() {}
  Formula(FormulaAST ast);

  bool satisfiable();
};

}

#endif
