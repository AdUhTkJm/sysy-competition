#include "Passes.h"
#include <cstring>

using namespace sys;

// Returns:
//   auto [success, offset] = isAddrOf(...)
//
// Here `success` is true if the `addr` actually refers to `gName`,
// and `offset` is positive only if it's a constant.
//
// (The language prohibits negative offsets from an array.)
std::pair<bool, int> isAddrOf(Op *addr, const std::string &gName) {
  if (isa<GetGlobalOp>(addr))
    return { NAME(addr) == gName, 0 };
  
  if (isa<AddLOp>(addr)) {
    auto x = addr->getOperand(0).defining;
    auto y = addr->getOperand(1).defining;

    if (isa<IntOp>(x)) {
      auto [success, offset] = isAddrOf(y, gName);
      return { success, offset + V(x) };
    }

    if (isa<IntOp>(y)) {
      auto [success, offset] = isAddrOf(x, gName);
      return { success, offset + V(y) };
    }

    // Now x and y are both unknown.
    auto [sx, offsetx] = isAddrOf(x, gName);
    if (sx)
      return { true, -1 };
    auto [sy, offsety] = isAddrOf(y, gName);
    if (sy)
      return { true, -1 };
    return { false, -1 };
  }

  return { false, -1 };
}

void Globalize::runImpl(Region *region) {
  region->updatePreds();

  Builder builder;

  auto funcOp = region->getParent();
  const auto &fnName = NAME(funcOp);
  auto allocas = funcOp->findAll<AllocaOp>();
  std::vector<Op*> unused;

  int allocaCnt = 0;

  for (auto alloca : allocas) {
    auto size = SIZE(alloca);
    if (size <= 32)
      continue;

    builder.setToRegionStart(module->getRegion());

    int *data = new int[size / 4];
    memset(data, 0, size);
    // name like __main_1
    std::string gName = "__" + fnName + "_" + std::to_string(allocaCnt++);
    auto global = builder.create<GlobalOp>({
      new NameAttr(gName),
      new SizeAttr(size),
      // Note that this only refers to, rather than copies, `data`.
      new IntArrayAttr(data, size / 4),
    });

    builder.setToBlockStart(alloca->getParent()->nextBlock());
    auto get = builder.create<GetGlobalOp>({ new NameAttr(gName) });
    alloca->replaceAllUsesWith(get);
    unused.push_back(alloca);

    // Find the longest linear execution block chain,
    // i.e. where the block has only one successor and the successor has only one predecessor.
    // All code on it will be executed exactly once in order.
    BasicBlock *runner = region->getFirstBlock();
    bool shouldBreak = false;
    std::map<int, Op*> unknownOffsets;

    for (;;) {
      auto ops = runner->getOps();
      for (auto op : ops) {
        if (isa<StoreOp>(op)) {
          auto value = op->getOperand(0).defining;
          auto addr = op->getOperand(1).defining;
          // Don't forget that `offset` is byte offset.
          auto [success, offset] = isAddrOf(addr, gName);

          if (!isa<IntOp>(value)) {
            if (success && offset >= 0) {
              unknownOffsets[offset] = value;
              continue;
            }
            if (success && offset < 0) {
              shouldBreak = true;
              break;
            }
            continue;
          }

          int v = V(value);

          // We're storing to an unknown place. Break.
          if (success && offset < 0) {
            shouldBreak = true;
            break;
          }
          if (success) {
            data[offset / 4] = v;
            op->erase();
            continue;
          }
        }

        if (isa<LoadOp>(op)) {
          auto addr = op->getOperand().defining;
          auto [success, offset] = isAddrOf(addr, gName);

          // We're reading an unknown place. That prohibits any further stores from folding.
          // Therefore we should break.
          if (success && offset < 0) {
            shouldBreak = true;
            break;
          }

          // Otherwise, we can fold this read.
          if (success) {
            builder.setBeforeOp(op);
            Op *value = nullptr;
            if (unknownOffsets.count(offset))
              value = unknownOffsets[offset];
            else
              value = builder.create<IntOp>({ new IntAttr(data[offset / 4]) });
            op->replaceAllUsesWith(value);
            op->erase();
            continue;
          }
        }
      }

      if (shouldBreak)
        break;

      if (runner->getSuccs().size() != 1)
        break;

      BasicBlock *succ = *runner->getSuccs().begin();
      if (succ->getPreds().size() != 1)
        break;

      runner = succ;
    }

    // We need to update `allZero` of `data`.
    auto attr = global->get<IntArrayAttr>();
    for (int i = 0; i < attr->size; i++) {
      if (attr->vi[i] != 0) {
        attr->allZero = false;
        break;
      }
    }
  }

  for (auto alloca : unused)
    alloca->erase();
}

void Globalize::run() {
  auto funcs = collectFuncs();
  for (auto func : funcs) {
    if (!func->has<AtMostOnceAttr>())
      continue;

    // If the function is called at most once, move any alloca of size > 32 out of it.
    // I choose to preserve small arrays, as I guess it'd have a better data locality.
    runImpl(func->getRegion());
  }
}
