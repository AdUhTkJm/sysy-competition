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
    bool isFP = alloca->has<FPAttr>();
    if (size <= 32)
      continue;

    builder.setToRegionStart(module->getRegion());

    void *data = isFP ? (void*) new float[size / 4] : new int[size / 4];
    memset(data, 0, size);
    // name like __main_1
    std::string gName = "__" + fnName + "_" + std::to_string(allocaCnt++);
    auto global = builder.create<GlobalOp>({
      new NameAttr(gName),
      new SizeAttr(size),
      // Note that this only refers to, rather than copies, `data`.
      isFP
        ? (Attr*) new FloatArrayAttr((float*) data, size / 4)
        : (Attr*) new IntArrayAttr((int*) data, size / 4),
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

          // Storing to an unknown place. Continue.
          if (success && offset < 0) {
            shouldBreak = true;
            break;
          }
          // This isn't storing to the address we're processing.
          if (!success)
            continue;

          // Floating point case.
          if (isFP) {
            if (!isa<FloatOp>(value)) {
              if (offset >= 0)
                unknownOffsets[offset] = value;
              continue;
            }

            float v = F(value);

            ((float*) data)[offset / 4] = v;
            op->erase();
            continue;
          }

          // Integer case.
          if (!isa<IntOp>(value)) {
            if (offset >= 0)
              unknownOffsets[offset] = value;
            continue;
          }

          int v = V(value);

          ((int*) data)[offset / 4] = v;
          op->erase();
          continue;
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
            else if (isFP)
              value = builder.create<FloatOp>({ new FloatAttr(((float*) data)[offset / 4]) });
            else
              value = builder.create<IntOp>({ new IntAttr(((int*) data)[offset / 4]) });
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
    if (isFP) {
      auto attr = global->get<FloatArrayAttr>();
      for (int i = 0; i < attr->size; i++) {
        if (attr->vf[i] != 0) {
          attr->allZero = false;
          break;
        }
      }
    } else {
      auto attr = global->get<IntArrayAttr>();
      for (int i = 0; i < attr->size; i++) {
        if (attr->vi[i] != 0) {
          attr->allZero = false;
          break;
        }
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

    // If the function is called at most once, move allocas out of it.
    // It's unsound to raise size = 8, because it might be holding address of some alloca.
    // And I choose to allow size <= 32. Hopefully better data locality?
    runImpl(func->getRegion());
  }
}
