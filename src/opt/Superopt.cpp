#include "SMTPasses.h"
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
std::vector<int> inputs { 0, 1, -1, 20050704 };

// For recursive functions & loops, we do the following:
// keep the "recursive function" as a hole, and substitute it with
// the synthesized expression when we try to evaluate or prove.
// This is sound as long as at least one argument monotonically decreases.

BvExprContext ctx;

// Collect all values used by `op` and build a BvExpr.
BvExpr *trace(Op *op) {
  static std::unordered_map<int, BvExpr::Type> mapping = {
    { AddIOp::id, BvExpr::Add },
    { SubIOp::id, BvExpr::Sub },
    { MulIOp::id, BvExpr::Mul },
  };
  switch (op->opid) {
  case AddIOp::id: {
    auto l = trace(op->DEF(0));
    auto r = trace(op->DEF(1));
    return ctx.create(BvExpr::Add, l, r);
  }
  }
}

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

  for (auto func : funcs) {
    auto region = func->getRegion();
    rewireExit(region);

    auto last = region->getLastBlock();
    auto ret = last->getLastOp();
    assert(isa<ReturnOp>(ret));
    queue.push_back(ret);

    auto stores = func->findAll<StoreOp>();
    for (auto store : stores) {
      // Do basic filtering.
      auto def = store->DEF(0);

      // Simple operations don't need optimization.
      if (isa<IntOp>(def))
        continue;

      // FP operations aren't supported.
      if (def->getResultType() != Value::i32)
        continue;

      queue.push_back(store->DEF(0));
    }
  }
}
