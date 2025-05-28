#include "CleanupPasses.h"
#include <unordered_set>

using namespace sys;

std::map<std::string, int> AggressiveDCE::stats() {
  return {
    { "removed-ops", elim },
  };
}

#define PRESERVED(Ty) || isa<Ty>(op)
static bool preserved(Op *op) {
  return (isa<CallOp>(op) && op->has<ImpureAttr>())
    PRESERVED(BranchOp)
    PRESERVED(GotoOp)
    PRESERVED(StoreOp)
    PRESERVED(ReturnOp);
}

void AggressiveDCE::runImpl(FuncOp *fn) {
  auto rets = fn->findAll<ReturnOp>();
  auto calls = fn->findAll<CallOp>();
  auto stores = fn->findAll<StoreOp>();
  auto branches = fn->findAll<BranchOp>();

  std::unordered_set<Op*> live;
  std::vector<Op*> queue(rets.begin(), rets.end());
  for (auto call : calls) {
    if (call->has<ImpureAttr>())
      queue.push_back(call);
  }
  for (auto store : stores)
    queue.push_back(store);
  for (auto branch : branches)
    queue.push_back(branch);

  // Find all reachable operations from the outside-used ones.
  while (!queue.empty()) {
    auto op = queue.back();
    queue.pop_back();

    if (live.count(op))
      continue;
    live.insert(op);

    for (auto operand : op->getOperands())
      queue.push_back(operand.defining);
  }

  // Remove every operation that isn't live.
  auto region = fn->getRegion();
  std::vector<Op*> remove;
  for (auto bb : region->getBlocks()) {
    for (auto op : bb->getOps()) {
      if (!preserved(op) && !live.count(op)) {
        op->removeAllOperands();
        remove.push_back(op);
      }
    }
  }

  elim += remove.size();
  for (auto op : remove)
    op->erase();
}

void AggressiveDCE::run() {
  auto funcs = collectFuncs();
  for (auto func : funcs)
    runImpl(func);
}
