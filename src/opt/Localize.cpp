#include "Passes.h"

using namespace sys;

void Localize::run() {
  CallGraph(module).run();

  // Identify functions called at most once.
  std::set<FuncOp*> atMostOnce;
  auto funcs = collectFuncs();
  auto fnMap = getFunctionMap();

  // The pass would be called multiple times,
  // as Inline will reveal new opportunities of localization.
  if (!beforeFlatten)
    // We have lost structured control flow.
    // The only thing we can guarantee is that main() is called at most once.
    atMostOnce = { fnMap["main"] };
  else for (auto func : funcs) {
    const auto &callers = func->get<CallerAttr>()->callers;

    if (callers.size() == 0) {
      atMostOnce.insert(func);
      continue;
    }

    if (callers.size() > 2)
      continue;

    FuncOp *caller = fnMap[callers[0]];
    const auto &selfName = func->get<NameAttr>()->name;
    // Recursive functions aren't candidates.
    if (caller == func)
      continue;

    auto calls = caller->findAll<CallOp>();
    bool good = true;
    CallOp *call = nullptr;

    // First, make sure there's only one call that calls the function.
    for (auto op : calls) {
      if (op->get<NameAttr>()->name == selfName) {
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
    atMostOnce.insert(func);
  }

  auto getglobs = module->findAll<GetGlobalOp>();
  auto gMap = getGlobalMap();
  std::map<GlobalOp*, std::set<FuncOp*>> accessed;

  Builder builder;

  for (auto get : getglobs) {
    const auto &name = get->get<NameAttr>()->name;
    accessed[gMap[name]].insert(get->getParentOp<FuncOp>());
  }

  for (auto [name, k] : gMap) {
    // We don't want to localize an array. In fact, we hope to globalize them.
    if (k->get<SizeAttr>()->value != 4)
      continue;

    if (!accessed.count(k)) {
      // The global variable is never accessed. Remove it.
      k->erase();
      continue;
    }

    auto v = accessed[k];
    if (v.size() > 1)
      continue;

    if (!atMostOnce.count(*v.begin()))
      continue;

    // Now we can replace the global with a local variable.
    auto user = *v.begin();
    auto region = user->getRegion();

    auto entry = region->getFirstBlock();
    Op *addr;
    if (beforeFlatten) {
      builder.setToBlockEnd(entry);
      addr = builder.create<AllocaOp>();
      
      auto bb = region->insertAfter(entry);
      // We must make sure the whole entry block contains only alloca.
      // This is also for further transformations that append allocas to the first block.
      builder.setToBlockStart(bb);
      auto init = builder.create<IntOp>({
        new IntAttr(k->get<IntArrayAttr>()->vi[0])
      });
      builder.create<StoreOp>({ init, addr });
    } else {
      // Remember to supply terminators for after FlattenCFG.
      builder.setBeforeOp(entry->getLastOp());
      addr = builder.create<AllocaOp>();

      auto bb = region->insertAfter(entry);
      builder.setToBlockStart(bb);
      auto init = builder.create<IntOp>({
        new IntAttr(k->get<IntArrayAttr>()->vi[0])
      });
      builder.create<StoreOp>({ init, addr });
      entry->getLastOp()->moveToEnd(bb);

      builder.setToBlockEnd(entry);
      builder.create<GotoOp>({ new TargetAttr(bb) });
    }

    // Replace all "getglobal" to use the addr instead.
    auto gets = user->findAll<GetGlobalOp>();
    for (auto get : gets) {
      if (get->get<NameAttr>()->name == name) {
        get->replaceAllUsesWith(addr);
        get->erase();
      }
    }
  }
}
