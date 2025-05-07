#include "LoopPasses.h"
#include "Passes.h"

using namespace sys;

int satadd(int a, int b) {
  int64_t x = a;
  x += b;
  if (x > INT_MAX)
    return INT_MAX;
  if (x < INT_MIN)
    return INT_MIN;
  return x;
}

using IRange = std::pair<int, int>;

IRange join(IRange l, IRange r) {
  auto [llow, lhigh] = l;
  auto [rlow, rhigh] = r;
  return std::make_pair(std::max(llow, rlow), std::max(lhigh, rhigh));
}

bool calculateRange(Op *op, bool widen) {
  if (isa<IntOp>(op)) {
    if (op->has<RangeAttr>())
      return false;

    int value = V(op);
    op->add<RangeAttr>(value, value);
    return true;
  }

  if (isa<AddIOp>(op)) {
    auto l = op->getOperand(0).defining;
    auto r = op->getOperand(1).defining;

    if (l->has<RangeAttr>() && r->has<RangeAttr>()) {
      auto [llow, lhigh] = RANGE(l);
      auto [rlow, rhigh] = RANGE(r);
      auto range = std::make_pair(satadd(llow, rlow), satadd(lhigh, rhigh));
      if (auto rangeAttr = op->find<RangeAttr>()) {
        auto xrange = join(range, rangeAttr->range);
        // Unchanged.
        if (xrange.first == range.first && xrange.second == range.second)
          return false;

        op->remove<RangeAttr>();
      }

      op->add<RangeAttr>(range);
      return true;
    }
  }

  return false;
}

void Range::runImpl(Region *region, const LoopForest &forest) {
  headers.clear();
  for (auto loop : forest.getLoops())
    headers.insert(loop->getHeader());

  auto entry = region->getFirstBlock();

  std::vector<BasicBlock*> worklist { entry };
  while (!worklist.empty()) {
    auto bb = worklist.back();
    worklist.pop_back();

    bool changed = false;
    bool isHeader = headers.count(bb);
    
    for (auto op : bb->getOps()) {
      if (calculateRange(op, isHeader))
        changed = true;
    }

    if (changed) {
      for (auto succ : bb->getSuccs())
        worklist.push_back(succ);
    }
  }
}

void Range::run() {
  LoopAnalysis loop(module);
  loop.run();
  auto forests = loop.getResult();

  auto funcs = collectFuncs();

  for (auto func : funcs)
    runImpl(func->getRegion(), forests[func]);
}
