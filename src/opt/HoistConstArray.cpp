#include "Passes.h"
#include <cstring>

using namespace sys;

std::map<std::string, int> HoistConstArray::stats() {
  return {
    { "hoisted-arrays", hoisted }
  };
}

// Warning: buggy. "all values are constants" part hasn't been implemented.
void HoistConstArray::attemptHoist(Op *op) {
  // A alloca is deemed constant if we statically know that all its elements are stored to, 
  // and are stored to only once, and all values are constants.
  // (That's too restrictive; perhaps this won't be much gain?)
  auto func = op->getParentOp();
  auto stores = func->findAll<StoreOp>();
  auto size = SIZE(op);
  int elem = size / 4;
  std::vector<int> value(elem);
  std::vector<float> fvalue(elem);
  std::vector<char> visited(elem);
  std::vector<Op*> toErase;

  for (auto store : stores) {
    auto addr = store->DEF(1);
    // An unknown store.
    if (!addr->has<AliasAttr>())
      return;

    auto alias = ALIAS(addr);
    if (!alias->location.count(op))
      continue;

    // An unsure store.
    if (alias->location.size() > 1)
      return;

    // Also an unsure store.
    const auto &offsets = alias->location[op];
    if (offsets.size() > 1 || offsets[0] == -1)
      return;

    // Now we already know where has been stored.
    int offset = offsets[0];
    visited[offset / 4] = 1;

    // We might also know the value.
    auto def = store->DEF(0);
    if (isa<IntOp>(def)) {
      value[offset / 4] = V(def);
      toErase.push_back(store);
    }
    if (isa<FloatOp>(def)) {
      fvalue[offset / 4] = F(def);
      toErase.push_back(store);
    }
  }

  for (int i = 0; i < size / 4; i++) {
    // Not every place has been stored.
    if (!visited[i])
      return;
  }

  bool fp = op->has<FPAttr>();

  // Now safe to transform.
  Builder builder;
  builder.setToRegionStart(module->getRegion());
  auto name = "__const_" + NAME(func) + "_" + std::to_string(hoisted++);
  auto global = builder.create<GlobalOp>({ new NameAttr(name), new SizeAttr(elem * 4) });

  // Add init value.
  if (fp) {
    float *vf = new float[elem];
    memcpy(vf, fvalue.data(), elem * sizeof(float));
    global->add<FloatArrayAttr>(vf, elem);
  } else {
    int *vi = new int[elem];
    memcpy(vi, value.data(), elem * sizeof(int));
    global->add<IntArrayAttr>(vi, elem);
  }

  // Replace the alloca with getglobal.
  auto entry = func->getRegion()->getFirstBlock();
  // Don't disrupt the consecutive allocas at the front.
  Op *firstNonAlloca = entry->getFirstOp();
  while (isa<AllocaOp>(firstNonAlloca))
    firstNonAlloca = firstNonAlloca->nextOp();

  builder.setBeforeOp(firstNonAlloca);
  auto getglobal = builder.create<GetGlobalOp>({ new NameAttr(name) });
  op->replaceAllUsesWith(getglobal);
  op->erase();

  // Erase redundant stores.
  for (auto x : toErase)
    x->erase();
}

void HoistConstArray::run() {
  auto allocas = module->findAll<AllocaOp>();
  for (auto alloca : allocas)
    attemptHoist(alloca);
}
