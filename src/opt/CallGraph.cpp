#include "Passes.h"

using namespace sys;

void CallGraph::run() {
  // Construct a call graph.
  // Actually Pureness can rely on this, but as it runs I wouldn't bother to change.
  std::map<std::string, std::set<std::string>> calledBy;

  auto calls = module->findAll<CallOp>();
  for (auto call : calls) {
    auto func = call->getParentOp<FuncOp>();
    auto calledName = call->getAttr<NameAttr>()->name;
    if (!isExtern(calledName))
      calledBy[calledName].insert(func->getAttr<NameAttr>()->name);
  }

  auto toplevel = module->getRegion()->getFirstBlock()->getOps();
  for (auto op : toplevel) {
    if (!isa<FuncOp>(op))
      continue;

    // Remove the old version.
    if (op->hasAttr<CallerAttr>())
      op->removeAttr<CallerAttr>();

    const auto &name = op->getAttr<NameAttr>()->name;
    const auto &callersSet = calledBy[name];
    std::vector<std::string> callers(callersSet.begin(), callersSet.end());
    op->addAttr<CallerAttr>(callers);
  }
}
