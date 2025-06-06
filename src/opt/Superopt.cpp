#include "SMTPasses.h"
#include "LoopPasses.h"
#include "../auto-gen/fn-1.h"
#include "../auto-gen/fn-2.h"
#include "../utils/smt/BvExpr.h"

using namespace sys;
using namespace smt;

std::map<std::string, int> Superopt::stats() {
  return {
    { "optimized", optimized }
  };
}

namespace {

// Idea (from Google, Souper, https://arxiv.org/pdf/1711.04422):
// Try these inputs on the synthesized expression to obtain constants.
// When that fails, the candidate fails;
// otherwise materialize the constants and try to prove it with SMT.
//
// NOTE: Currently this is NOT done because there might be more than one argument.
std::vector<int> inputs { 0, 1, -1, 20050704 };

// For recursive functions & loops, we do the following:
// keep the "recursive function" as a hole, and substitute it with
// the synthesized expression when we try to evaluate or prove.
// This is sound as long as at least one argument monotonically decreases.

// The current function name.
std::string fname;

}

Superopt::Superopt(ModuleOp *module): Pass(module) {
  
}

bool Superopt::fillPredicate(Region *region) {
  std::vector<BasicBlock*> worklist;
  std::unordered_set<BasicBlock*> visited;

  // Constant 1 represents true. This needs special treatment in BvExpr.
  // (It only accepts single-bit variable.)
  auto entry = region->getFirstBlock();
  blockpred[entry] = ctx.create(BvExpr::Const, 1);
  worklist.push_back(entry);

  const auto &propagate = [&](BasicBlock *bb, BvExpr *incoming) {
    auto &existing = blockpred[bb];
    if (!existing)
      existing = incoming;
    else
      existing = ctx.create(BvExpr::Or, existing, incoming);

    worklist.push_back(bb);
  };

  while (!worklist.empty()) {
    auto bb = worklist.back();
    worklist.pop_back();

    if (visited.count(bb))
      continue;
    visited.insert(bb);

    BvExpr *pred = blockpred[bb];
    auto term = bb->getLastOp();

    if (isa<GotoOp>(term))
      propagate(TARGET(term), pred);
    
    if (isa<BranchOp>(term)) {
      BvExpr *cond = trace(term->DEF());
      if (!cond && std::cerr << term->DEF())
        return false;

      propagate(TARGET(term), ctx.create(BvExpr::And, pred, cond));
      propagate(ELSE(term), ctx.create(BvExpr::And, pred, ctx.create(BvExpr::Not, cond)));
    }
  }
  return true;
}

BvExpr *Superopt::trace(Op *op) {
  // Memoization.
  if (cache.count(op))
    return cache[op];

  static std::unordered_map<int, BvExpr::Type> mapping = {
    { AddIOp::id, BvExpr::Add },
    { SubIOp::id, BvExpr::Sub },
    { MulIOp::id, BvExpr::Mul },
    { DivIOp::id, BvExpr::Div },
    { ModIOp::id, BvExpr::Mod },
    { EqOp::id, BvExpr::Eq },
    { NeOp::id, BvExpr::Ne },
    { LeOp::id, BvExpr::Le },
    { LtOp::id, BvExpr::Lt },
  };

  // Handle basic binary operations.
  if (mapping.count(op->opid)) {
    auto l = trace(op->DEF(0));
    auto r = trace(op->DEF(1));
    if (!l || !r)
      return nullptr;

    auto expr = ctx.create(mapping[op->opid], l, r);
    return cache[op] = expr;
  }

  // Handle call operations; only consider self-recursion, and deny all other calls
  if (isa<CallOp>(op)) {
    const auto &name = NAME(op);
    if (fname == name) {
      auto hole = ctx.create(BvExpr::Hole);
      int argcnt = op->getOperandCount();
      if (argcnt >= 1)
        hole->cond = trace(op->DEF(0));
      if (argcnt >= 2)
        hole->l = trace(op->DEF(1));
      if (argcnt >= 3)
        hole->r = trace(op->DEF(2));

      return cache[op] = hole;
    }

    return nullptr;
  }

  // Handle phi nodes
  if (isa<PhiOp>(op)) {
    std::vector<std::pair<BvExpr*, BvExpr*>> cases; // (condition, value)

    for (int i = 0; i < op->getOperandCount(); ++i) {
      auto def = op->DEF(i);
      BasicBlock *bb = FROM(op->getAttrs()[i]);
      BvExpr *val = trace(def);
      if (!val)
        return nullptr;

      cases.emplace_back(blockpred[bb], val);
    }

    // Build nested ITEs: ite(cond0, val0, ite(cond1, val1, ...))
    BvExpr *expr = cases.back().second;
    for (int i = (int)cases.size() - 2; i >= 0; --i) {
      auto [cond, val] = cases[i];
      expr = ctx.create(BvExpr::Ite, cond, val, expr);
    }

    return cache[op] = expr;
  }

  // Handle constant.
  if (isa<IntOp>(op))
    return cache[op] = ctx.create(BvExpr::Const, V(op));
  
  // Handle parameters.
  if (isa<GetArgOp>(op))
    return cache[op] = ctx.create(BvExpr::Var, "arg" + std::to_string(V(op)));

  // Unsupported.
  return nullptr;
}

// Make sure the region has a single exit.
// This enables optimization for return values.
void Superopt::rewireExit(Region *region) {
  std::vector<BasicBlock*> exits;
  for (auto bb : region->getBlocks()) {
    if (isa<ReturnOp>(bb->getLastOp()))
      exits.push_back(bb);
  }
  if (exits.size() > 1) {
    Builder builder;
    auto exit = region->appendBlock();
    builder.setToBlockStart(exit);

    // We have a return value. Create a phi to record it.
    if (exits[0]->getLastOp()->getOperands().size() > 0) {
      auto phi = builder.create<PhiOp>();
      for (auto bb : exits) {
        auto ret = bb->getLastOp()->getOperand();
        phi->pushOperand(ret);
        phi->add<FromAttr>(bb);
      }
      builder.create<ReturnOp>({ phi });
    } else {
      // Just a normal return.
      builder.create<ReturnOp>();
    }

    // Rewire all exits to the new exit.
    for (auto bb : exits)
      builder.replace<GotoOp>(bb->getLastOp(), { new TargetAttr(exit) });
  }
}

void Superopt::run() {
  std::vector<Op*> queue;
  // We optimize everything that:
  // 1) is used as the value of store;
  // 2) is used as a result calculated inside a loop.
  auto funcs = collectFuncs();

  LoopAnalysis analysis(module);

  for (auto func : funcs) {
    // Currently we only test for function with <= 2 arguments.
    // For 2 arguments there'll be ~3500 expression to try,
    // whereas for 3 arguments this gets to ~12000, so it's unmanageable.
    int argcnt = func->get<ArgCountAttr>()->count;
    if (argcnt > 2)
      continue;
    if (func->has<ImpureAttr>())
      continue;

    // If the function contains loop we'll treat them separately.
    auto region = func->getRegion();
    auto forest = analysis.runImpl(region);
    if (forest.getLoops().size())
      continue;

    rewireExit(region);

    auto last = region->getLastBlock();
    auto ret = last->getLastOp();
    if (!ret->getOperandCount())
      continue;
    
    assert(isa<ReturnOp>(ret));
    queue.push_back(ret);

    fname = NAME(func);
    blockpred.clear();
    if (!fillPredicate(region))
      continue;

    auto expr = trace(ret->DEF());
    if (!expr)
      continue;
    // No need for things this simple.
    if (isa<IntOp>(expr))
      continue;

    std::cerr << expr << "\n";

    const std::vector<BvExpr*> &candidates = argcnt == 1 ? candidates_1 : candidates_2;
    for (auto candidate : candidates) {
      BvSolver solver;
      auto filled = fillHole(expr, candidate);
      auto eq = ctx.create(BvExpr::Eq, filled, candidate);
      solver.infer(eq);

      // Extract a model and prove equality.
      // TODO
    }
  }
}
