#include "PreAnalysis.h"

using namespace sys;

void NoStore::runImpl(Op *func) {
  auto stores = func->findAll<StoreOp>();
  for (auto store : stores) {
    auto addr = store->DEF(1);
    if (!addr->has<BaseAttr>())
      return;
    auto base = BASE(addr);
    if (isa<GetGlobalOp>(base))
      return;
  }
  if (!func->has<NoStoreAttr>())
    func->add<NoStoreAttr>();
}

void NoStore::run() {
  Base(module).run();

  auto funcs = collectFuncs();
  for (auto func : funcs)
    runImpl(func);
}
