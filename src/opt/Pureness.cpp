#include "Analysis.h"

using namespace sys;

void Pureness::run() {
  auto funcs = collectFuncs();

  // Construct a call graph.
  auto fnMap = getFunctionMap();
  auto calls = module->findAll<CallOp>();
  for (auto call : calls) {
    auto func = call->getParentOp<FuncOp>();
    auto calledName = NAME(call);
    if (!isExtern(calledName))
      callGraph[func].insert(fnMap[calledName]);
    else if (!func->has<ImpureAttr>())
      // External functions are impure.
      func->add<ImpureAttr>();
  }

  // Every function that accesses globals is impure.
  for (auto func : funcs) {
    if (!func->has<ImpureAttr>() && !func->findAll<GetGlobalOp>().empty())
      func->add<ImpureAttr>();
  }

  // Propagate impureness across functions:
  // if a functions calls any impure function then it becomes impure.
  bool changed;
  do {
    changed = false;
    for (auto func : funcs) {
      bool impure = false;
      for (auto v : callGraph[func]) {
        if (v->has<ImpureAttr>()) {
          impure = true;
          break;
        }
      }
      if (!func->has<ImpureAttr>() && impure) {
        changed = true;
        func->add<ImpureAttr>();
      }
    }
  } while (changed);
}
