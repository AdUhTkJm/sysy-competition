#include "Analysis.h"

using namespace sys;

void CallGraph::run() {
  // Construct a call graph.
  // Actually Pureness can rely on this, but as it runs I wouldn't bother to change.
  std::map<std::string, std::set<std::string>> calledBy;

  auto calls = module->findAll<CallOp>();
  // We consider `clone()` syscall also as calling the worker function.
  auto workers = module->findAll<CloneOp>();
  calls.reserve(calls.size() + workers.size());
  std::copy(workers.begin(), workers.end(), std::back_inserter(calls));
  for (auto call : calls) {
    auto func = call->getParentOp<FuncOp>();
    auto calledName = NAME(call);
    if (!isExtern(calledName))
      calledBy[calledName].insert(NAME(func));
  }

  auto funcs = collectFuncs();
  for (auto func : funcs) {
    // Remove the old version.
    func->remove<CallerAttr>();

    const auto &name = NAME(func);
    const auto &callersSet = calledBy[name];
    std::vector<std::string> callers(callersSet.begin(), callersSet.end());
    func->add<CallerAttr>(callers);
  }
}
