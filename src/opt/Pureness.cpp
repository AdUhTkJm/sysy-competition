#include "Passes.h"
#include "../codegen/Attrs.h"

using namespace sys;

// Find all stores to an address.
// An address might be loaded, stored or added by some offset.
bool hasStoresTo(Op *op) {
  for (auto use : op->getUses()) {
    // This checks both the case when the address is stored elsewhere,
    // and the value at the address is mutated.
    // The language seems to prevent the first case, but it doesn't matter.
    if (isa<StoreOp>(use))
      return true;

    // It's a new address. Find all stores there.
    if (isa<AddIOp>(use) || isa<AddLOp>(use)) {
      if (hasStoresTo(use))
        return true;
      continue;
    }

    if (isa<LoadOp>(use))
      continue;

    // If something else happens, then it isn't an address.
    return false;
  }

  return false;
}

// A function is impure if one of the following holds:
//   1) it stores to one of its arguments;
//   2) it reads/writes a global variable;
//   3) it calls another impure function.
// We determine 1) here.
void Pureness::predetermineImpure(FuncOp *func) {
  auto args = func->findAll<GetArgOp>();
  for (auto arg : args) {
    // The only use of GetArgOp should be to store it into some Alloca.
    assert(arg->getUses().size() == 1);
    auto store = *arg->getUses().begin();
    auto addr = store->getOperand(1).defining;

    // The addr can only be loaded or stored.
    // The language does not allow offsetting this pointer or storing it elsewhere.
    for (auto use : addr->getUses()) {
      if (!isa<LoadOp>(use)) {
        assert(isa<StoreOp>(use));
        continue;
      }

      // This is a load, retrieving the underlying argument, which is an address.
      // If anything stores to the address then the function is impure.
      if (hasStoresTo(use) && !func->hasAttr<ImpureAttr>()) {
        func->addAttr<ImpureAttr>();
        return;
      }
    }
  }
}

void Pureness::run() {
  auto funcs = module->findAll<FuncOp>();

  // Note that predetermineImpure() assumes to operate **before** Mem2Reg,
  // and any further transformation should preserve function pureness.
  // For pureness of individual functions, do it in DCE.

  // Construct a call graph.
  auto calls = module->findAll<CallOp>();
  for (auto call : calls) {
    auto func = call->getParentOp<FuncOp>();
    auto calledName = call->getAttr<NameAttr>()->name;
    if (!isExtern(calledName))
      callGraph[func].insert(findFunction(calledName));
    else if (!func->hasAttr<ImpureAttr>())
      // External functions are impure.
      func->addAttr<ImpureAttr>();
  }

  // Predetermine all other factors that make a function impure,
  // expect the criterion of "calling another impure function".
  for (auto func : funcs)
    predetermineImpure(func);

  // Every function that accesses globals is impure.
  for (auto func : funcs) {
    if (!func->hasAttr<ImpureAttr>() && !func->findAll<GetGlobalOp>().empty())
      func->addAttr<ImpureAttr>();
  }

  // Propagate impureness across functions:
  // if a functions calls any impure function then it becomes impure.
  bool changed;
  do {
    changed = false;
    for (auto func : funcs) {
      bool impure = false;
      for (auto v : callGraph[func]) {
        if (v->hasAttr<ImpureAttr>()) {
          impure = true;
          break;
        }
      }
      if (!func->hasAttr<ImpureAttr>() && impure) {
        changed = true;
        func->addAttr<ImpureAttr>();
      }
    }
  } while (changed);
}
