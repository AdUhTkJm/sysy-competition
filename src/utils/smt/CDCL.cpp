#include "CDCL.h"
#include <algorithm>
#include <set>
#include <cassert>
#include <unordered_set>

using namespace smt;

// Get the variable inside the atomic.
#define VAR(atom) ((atom) >> 1)
#define BOOLNOT(logic) (-(logic))
#define ATOMNOT(atom) ((atom) ^ 1)
#define ISNEG(atom) ((atom) & 1)

void dumpAtomic(std::ostream &os, Atomic atom) {
  auto var = VAR(atom) + 1;
  if (ISNEG(atom))
    os << "!" << var;
  else
    os << var;
}

void Clause::dump(std::ostream &os) const {
  os << "{ ";
  if (content.size() > 0)
    dumpAtomic(os, content[0]);
  for (int i = 1; i < content.size(); i++) {
    os << ", ";
    dumpAtomic(os, content[i]);
  }
  os << " } (watching: ";
  dumpAtomic(os, watch[0]);
  os << ", ";
  dumpAtomic(os, watch[1]);
  os << ")";
}

void Solver::dump(std::ostream &os) const {
  os << "varcnt = " << varcnt << "\n";
  for (auto x : clauses)
    os << x << "\n";
  for (int i = 0; i < varcnt; i++)
    os << i + 1 << " = " << (int) assignment[i] << "\n";
}

void dumpArray(const std::vector<Atomic> &content) {
  auto &os = std::cerr;
  os << "{ ";
  if (content.size() > 0)
    dumpAtomic(os, content[0]);
  for (int i = 1; i < content.size(); i++) {
    os << ", ";
    dumpAtomic(os, content[i]);
  }
  os << " }";
}

Solver::Boolean Solver::value(Atomic atom) {
  Variable var = VAR(atom);
  Boolean logic = assignment[var];
  return ISNEG(atom) ? BOOLNOT(logic) : logic;
}

// Refer to DPLL. It triggers when only one literal is unassigned, and all others are 0.
// Unit propagation tries to build up edges in implied graph, and also detect conflicts.
void Solver::unitPropagation() {
  while (qhead < trail.size()) {
    Atomic atom = trail[qhead++];
    Atomic neg = ATOMNOT(atom);

    // In unit propagation, the things we pushed onto queue must have been true.
    // When the atomic is (!v), then v must be false;
    // When the atomic is v, then v must be true.
    assignment[VAR(atom)] = ISNEG(atom) ? False : True;

    // Therefore, `neg` is false. Check whether we have something watched on it.
    auto &watches = watched[neg];
    for (int i = 0; i < watches.size();) {
      auto clause = watches[i];

      // Find the other watched literal of the clause.
      int watchid = neg != clause->watch[0];
      Atomic other = clause->watch[!watchid];
      
      // The clause has been satisfied.
      if (other != -1 && value(other) == True) {
        i++;
        continue;
      }

      Atomic watch = -1;
      // Try to find another literal to watch in the clause.
      for (auto atomic : clause->content) {
        if (atomic == neg || atomic == other)
          continue;

        if (value(atomic) != False) {
          watch = atomic;
          break;
        }
      }

      if (watch != -1) {
        clause->watch[watchid] = watch;
        // Remove the current clause from watchers of `neg`.
        watches[i] = watches.back();
        watches.pop_back();
        // Add the current clause to watchers of `watch`.
        watched[watch].push_back(clause);
        // Don't increment `i` because the current element has changed to a new one.
        continue;
      } else {
        if (other == -1 || value(other) == False) {
          // Conflict.
          unitConflict = true;
          conflictClause = clause;
          return;
        }

        // Trigger a new round of unit propagation.
        if (value(other) == Unassigned) {
          enqueue(other, clause);
          i++;
        }
      }
    }
  }
}

std::pair<int, std::vector<Atomic>> Solver::analyzeConflict() {
  if (!dl)
    return { -1, {} };

  const auto &conflict = conflictClause->content;
  std::unordered_set<Atomic> learnt(conflict.begin(), conflict.end());
  // Atomics of the conflicting clause at current decision level.
  std::unordered_set<Atomic> atomics;
  for (auto atom : conflictClause->content) {
    if (decisionLevel[VAR(atom)] == dl)
      atomics.insert(atom);
  }

  while (atomics.size() != 1) {
    Atomic implied = -1;
    for (auto atom : atomics) {
      if (antecedent[VAR(atom)]) {
        implied = atom;
        break;
      }
    }

    assert(implied != -1);

    auto ante = antecedent[VAR(implied)];
    
    for (auto atom : ante->content)
      learnt.insert(atom);
    
    learnt.erase(implied);
    learnt.erase(ATOMNOT(implied));

    atomics.clear();
    for (auto atom : learnt) {
      if (decisionLevel[VAR(atom)] == dl)
        atomics.insert(atom);
    }
  }
  
  // Backtrack to the second largest decision level.
  // If that doesn't exist, backtrack to dl = 0.
  std::vector<Atomic> result(learnt.begin(), learnt.end());
  if (result.size() <= 1)
    return { 0, result };
  else {
    std::set<int, std::greater<int>> dls;
    for (auto x : result)
      dls.insert(decisionLevel[VAR(x)]);
    return { *++dls.begin(), result };
  }
}

Clause *Solver::addClause(const std::vector<Atomic> &clause) {
  if (clause.empty()) {
    addedConflict = true;
    return nullptr;
  }

  std::vector<Atomic> processed;
  bool sat = false;

  // To initialize watchers, we need to find 2 atomics that haven't been assigned false.
  Atomic nonfalse[2];
  int top = 0;

  // Remove already known values.
  for (Atomic atom : clause) {
    Boolean val = value(atom);
    if (val == True) {
      sat = true;
      break;
    }
    if (val != False) {
      processed.push_back(atom);
      if (top < 2)
        nonfalse[top++] = atom;
    }
  }
  if (sat)
    return nullptr;

  if (processed.empty()) {
    addedConflict = true;
    return nullptr;
  }

  if (top == 1)
    nonfalse[1] = -1;

  Clause *added = new Clause { .content = processed, .watch = { nonfalse[0], nonfalse[1] } };
  clauses.push_back(added);

  // Set watchers.
  watched[nonfalse[0]].push_back(added);
  if (nonfalse[1] != -1)
    watched[nonfalse[1]].push_back(added);
  return added;
}

Clause *Solver::addLearnt(std::vector<Atomic> clause) {
  std::sort(clause.begin(), clause.end(), [&](Atomic a, Atomic b) {
    return decisionLevel[VAR(a)] > decisionLevel[VAR(b)];
  });
  
  if (clause.size() == 1) {
    Clause *added = new Clause { .content = clause, .watch = { clause[0], -1 } };
    clauses.push_back(added);

    watched[clause[0]].push_back(added);
    return added;
  }

  Clause *added = new Clause { .content = clause, .watch = { clause[0], clause[1] } };
  clauses.push_back(added);

  watched[clause[0]].push_back(added);
  watched[clause[1]].push_back(added);
  return added;
}

void Solver::enqueue(Atomic atom, Clause* ante) {
  int var = VAR(atom);
  if (assignment[var] != 0)
    return;
  
  assignment[var] = !ISNEG(atom) ? True : False;
  antecedent[var] = ante;
  decisionLevel[var] = dl;
  trail.push_back(atom);
}

void Solver::backtrack(int level) {
  // Remove all assignments done above the backtrack level.
  for (int i = 0; i < varcnt; i++) {
    if (decisionLevel[i] > level) {
      assignment[i] = Unassigned;
      antecedent[i] = nullptr;
      decisionLevel[i] = 0;
    }
  }
}

bool Solver::allAssigned() {
  for (auto x : assignment) {
    if (x == Unassigned)
      return false;
  }
  return true;
}

std::pair<Variable, Solver::Boolean> Solver::pickPivot() {
  // Pick the first unassigned one, for now.
  for (int i = 0; i < varcnt; i++) {
    if (assignment[i] == Unassigned)
      return { i, rand() & 1 ? True : False };
  }
  assert(false);
}

void Solver::init(int varcnt) {
  // These are indexed by variable.
  assignment.resize(varcnt);
  antecedent.resize(varcnt);
  decisionLevel.resize(varcnt);
  // These are index by atomics.
  watched.resize(varcnt * 2 + 1);

  unitConflict = false;

  this->varcnt = varcnt;
}

Atomic Solver::findUnit(const std::vector<Atomic> &unit) {
  int unassigned = 0;
  for (auto x : unit) {
    auto var = VAR(x);
    if (assignment[var] == Unassigned)
      return x;
  }
  assert(false);
}

// False for unsatisfiable.
bool Solver::solve(std::vector<signed char> &assignments) {
  assert(varcnt != -1);
  qhead = 0;
  dl = 0;

  if (addedConflict) {
    addedConflict = false;
    return false;
  }
  unitPropagation();
  if (unitConflict) {
    unitConflict = false;
    return false;
  }

  while (!allAssigned()) {
    auto [var, val] = pickPivot();
    dl++;
    Atomic atom = val == True ? (var << 1) : ((var << 1) + 1);
    enqueue(atom, nullptr);

    for (;;) {
      unitPropagation();

      if (!unitConflict)
        break;
      
      unitConflict = false;
      auto [btlvl, learnt] = analyzeConflict();
      if (btlvl < 0 || learnt.empty())
        return false;

      Clause *cl = addLearnt(learnt);
      backtrack(btlvl);
      dl = btlvl;

      trail.clear();
      Atomic atom = findUnit(learnt);
      // The variable must be true. Do a unit propagation from here.
      qhead = 0;
      enqueue(atom, cl);
    }
  }

  assignments.resize(varcnt);
  for (int i = 0; i < varcnt; i++)
    assignments[i] = (assignment[i] == True);
  
  return true;
}
