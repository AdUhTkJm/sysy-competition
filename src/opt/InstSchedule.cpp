#include "LowerPasses.h"
#include "Analysis.h"
#include <unordered_set>

using namespace sys;

void InstSchedule::runImpl(BasicBlock *bb) {
  for (auto op : bb->getOps()) {
    // We can't reschedule if there is any pinned operations.
    // TODO: Perhaps there's some way to mitigate this?
    if (isa<CallOp>(op) && op->has<ImpureAttr>())
      return;
  }

  // First, we need to build a dependence graph between loads/stores.
  std::vector<Op*> stores, loads;
  for (auto op : bb->getOps()) {

    // Check against store, but no need to check loads.
    if (isa<LoadOp>(op)) {
      auto addr = ALIAS(op->DEF());
      for (auto store : stores) {
        auto alias = ALIAS(store->DEF(1));
        if (addr->mayAlias(alias))
          op->pushOperand(store);
      }

      loads.push_back(op);
    }

    // Check both stores and loads.
    if (isa<StoreOp>(op)) {
      auto addr = ALIAS(op->DEF(1));

      for (auto store : stores) {
        auto alias = ALIAS(store->DEF(1));
        if (addr->mayAlias(alias))
          op->pushOperand(store);
      }

      for (auto load : loads) {
        auto alias = ALIAS(load->DEF());
        if (addr->mayAlias(alias))
          op->pushOperand(load);
      }

      stores.push_back(op);
    }
  }

  // Now do a list scheduling.

  // The amount of ops that this one is waiting for.
  std::unordered_map<Op*, int> degree;
  std::unordered_map<Op*, int> time;
  int index = 0;
  auto goodness = [&](Op *op) -> int {
    int result = 0;

    // Delay constants whenever possible.
    if (isa<IntOp>(op) || isa<FloatOp>(op) || isa<GetGlobalOp>(op))
      return -3000;

    // The numbers are somewhat arbitrary; we don't have a good understanding of BOOM.
    for (int i = 0; i < op->getOperandCount(); i++) {
      auto def = op->DEF(i);

      // In case the operation itself is a load/store, they're added with some extra operands.
      // We don't need to take them into account.
      if (isa<LoadOp>(op) && i >= 1)
        break;
      if (isa<StoreOp>(op) && i >= 2)
        break;
      
      // Wait 2 instructions for load.
      if (isa<LoadOp>(def) && index - time[def] <= 2) {
        result--;
      }

      // Wait 8 instructions for multiplication.
      if ((isa<MulIOp>(def) || isa<MulLOp>(def)) && index - time[def] <= 8) {
        result--;
      }
    }

    if (result < 0)
      return result;

    // Emit loads early.
    if (isa<LoadOp>(op))
      result += 8;
    
    // If the instruction is too far from its operands, then emit it early.
    // This is to lower the amount of live-range conflicts.
    for (auto operand : op->getOperands()) {
      auto def = operand.defining;
      // If the operand is live-in, then no need.
      if (time[def])
        result = std::max(result, (index - time[def]) / 3);
    }
    return result;
  };
  // The priority queue pops the LARGEST element by default.
  std::list<Op*> ready;
  std::unordered_set<Op*> allowed;

  for (auto op : bb->getOps()) {
    for (auto operand : op->getOperands()) {
      auto def = operand.defining;
      if (!bb->getLiveIn().count(def))
        ++degree[op];
    }
  }

  auto term = bb->getLastOp();
  for (auto op : bb->getOps()) {
    if (op != term && !isa<PhiOp>(op)) {
      allowed.insert(op);
      if (!degree[op])
        ready.push_back(op);
    }
  }

  while (!ready.empty()) {
    // Find the best element.
    decltype(ready)::iterator it;
    int best = INT_MIN;
    for (auto i = ready.begin(); i != ready.end(); i++) {
      auto good = goodness(*i);
      if (good > best) {
        it = i;
        best = good;
      }
    }
    Op *op = *it;
    ready.erase(it);

    op->moveBefore(term);
    time[op] = index++;

    for (auto use : op->getUses()) {
      // It's possible that a single operation refers to the same op more than once.
      for (auto operand : use->getOperands()) {
        if (operand.defining == op)
          --degree[use];
      }
      
      if (!degree[use] && allowed.count(use))
        ready.push_back(use);
    }
  }
  assert(index == allowed.size());

  // Don't forget to erase the introduced operands.
  for (auto load : loads) {
    auto addr = load->getOperand();
    load->removeAllOperands();
    load->pushOperand(addr);
  }

  for (auto store : stores) {
    auto value = store->getOperand(0);
    auto addr = store->getOperand(1);
    store->removeAllOperands();
    store->pushOperand(value);
    store->pushOperand(addr);
  }
}

void InstSchedule::run() {
  Alias(module).run();

  auto funcs = collectFuncs();
  for (auto func : funcs) {
    auto region = func->getRegion();
    region->updateLiveness();

    for (auto bb : region->getBlocks())
      runImpl(bb);
  }
}
