#include "Passes.h"

using namespace sys;

// This runs before Flatten CFG.
void AtMostOnce::run() {
  CallGraph(module).run();
  auto funcs = collectFuncs();
  auto fnMap = getFunctionMap();

  for (auto func : funcs) {
    if (func->has<AtMostOnceAttr>())
      continue;

    const auto &callers = CALLER(func);

    if (callers.size() == 0) {
      func->addAttr<AtMostOnceAttr>();
      continue;
    }

    if (callers.size() > 2)
      continue;

    FuncOp *caller = fnMap[callers[0]];
    const auto &selfName = NAME(func);
    // Recursive functions aren't candidates.
    if (caller == func)
      continue;

    auto calls = caller->findAll<CallOp>();
    bool good = true;
    CallOp *call = nullptr;

    // First, make sure there's only one call that calls the function.
    for (auto op : calls) {
      if (NAME(op) == selfName) {
        if (call) {
          good = false;
          break;
        }
        call = op;
      }
    }

    if (!good)
      continue;

    // Next, make sure the call isn't enclosed in a WhileOp.
    Op *father = call;
    while (!isa<FuncOp>(father)) {
      father = father->getParentOp();
      if (isa<WhileOp>(father)) {
        good = false;
        break;
      }
    }

    if (!good)
      continue;

    // Now we know the function is called at most once.
    func->addAttr<AtMostOnceAttr>();
  }
}