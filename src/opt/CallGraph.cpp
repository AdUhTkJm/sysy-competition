#include "Passes.h"

using namespace sys;

void CallGraph::run() {
  // Construct a call graph.
  // Actually Pureness can rely on this, but as it runs I wouldn't bother to change.
  std::map<std::string, std::set<std::string>> calledBy;

  auto calls = module->findAll<CallOp>();
  for (auto call : calls) {
    auto func = call->getParentOp<FuncOp>();
    auto calledName = call->get<NameAttr>()->name;
    if (!isExtern(calledName))
      calledBy[calledName].insert(func->get<NameAttr>()->name);
  }

  auto funcs = collectFuncs();
  for (auto func : funcs) {
    // Remove the old version.
    if (func->has<CallerAttr>())
      func->removeAttr<CallerAttr>();

    const auto &name = func->get<NameAttr>()->name;
    const auto &callersSet = calledBy[name];
    std::vector<std::string> callers(callersSet.begin(), callersSet.end());
    func->addAttr<CallerAttr>(callers);
  }
}
