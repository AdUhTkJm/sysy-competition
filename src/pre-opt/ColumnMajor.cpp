#include "PreLoopPasses.h"
#include "PreAnalysis.h"

using namespace sys;

namespace {

struct NoBase {};

// Finds the i'th ForOp counting from this one.
Op *findIndvar(Op *addr, int i) {
  if (i == 0)
    return addr;
  return findIndvar(addr->getParentOp<ForOp>(), i - 1);
}

}

void ColumnMajor::collectDepth(Region *region, int depth) {
  for (auto bb : region->getBlocks()) {
    for (auto op : bb->getOps()) {
      // Recursively explore the regions.
      if (op->getRegionCount()) {
        for (auto r : op->getRegions())
          collectDepth(r, depth + isa<ForOp>(op));
        continue;
      }

      // Collect address accessed.
      Op *addr = nullptr;
      if (isa<StoreOp>(op))
        addr = op->DEF(1);
      if (isa<LoadOp>(op))
        addr = op->DEF();
      if (!addr)
        continue;

      if (!addr->has<BaseAttr>())
        continue;
      auto base = BASE(addr);
      // Only the deepest loop nest matters.
      if (depth < data[base].depth)
        continue;

      // Clear all information in shallower places.
      if (depth > data[base].depth) {
        data[base].jumping = data[base].contiguous = false;
        data[base].depth = depth;
      }

      // Terminate the whole pass when there's no base.
      if (!base)
        throw NoBase{};

      if (!addr->has<SubscriptAttr>()) {
        data[base].valid = false;
        continue;
      }
      
      // We must ensure `addr` is never passed as an argument to somewhere.
      for (auto use : addr->getUses()) {
        if (isa<CallOp>(use)) {
          data[base].valid = false;
          break;
        }
      }

      if (!data[base].valid)
        continue;

      // It is not necessary that there are 2 non-zero entries in `subscript`.
      // For example, A[i][i] will only have one entry corresponding to `i`.
      const auto &subscript = SUBSCRIPT(addr);

      // The last subscript is for constant values. Ignore that.
      auto v = subscript[subscript.size() - 2];
      if (v == 0)
        continue;

      // See whether the last index of subscript is the smallest element (except zero).
      for (int i = 0; i < subscript.size() - 2; i++) {
        if (!subscript[i])
          continue;

        if (subscript[i] < v) {
          data[base].jumping = true;
          break;
        }
        if (subscript[i] > v) {
          data[base].contiguous = true;
          break;
        }
      }
    }
  }
}

void ColumnMajor::run() {
  Base(module).run();
  ArrayAccess(module).run();

  auto funcs = collectFuncs();
  try {
    for (auto func : funcs)
      collectDepth(func->getRegion(), 0);
  } catch (NoBase) { return; }

  std::vector<Op*> convert;
  // Collect all non-contiguous, valid entries.
  for (auto [base, access] : data) {
    if (access.valid && !access.contiguous && access.jumping)
      convert.push_back(base);
  }

  if (!convert.size())
    return;

  for (auto b : convert)
    std::cerr << "convert to column major: " << b;

  // Perform conversion.
  // We've ensured every access has a subscript here.
  auto loads = module->findAll<LoadOp>();
  auto stores = module->findAll<StoreOp>();
  std::vector<Op*> addrs;
  addrs.reserve(loads.size() + stores.size());
  for (auto x : loads)
    addrs.push_back(x->DEF());
  for (auto x : stores)
    addrs.push_back(x->DEF(1));

  std::vector<Op*> todelete;

  for (auto base : convert) {
    Builder builder;

    for (auto addr : addrs) {
      // We've ensured all addresses have a base in `collectDepth`.
      if (BASE(addr) != base)
        continue;

      // Find the ForOp fpr the corresponding subscripts for this address.
      const auto &subscript = SUBSCRIPT(addr);
      Op *outer = nullptr, *inner = nullptr;
      int v1, v2;
      for (int i = 0; i < subscript.size() - 1; i++) {
        if (subscript[i] == 0)
          continue;

        Op *&p = outer ? inner : outer;
        int &v = outer ? v2 : v1;
        p = findIndvar(addr, subscript.size() - 1 - i);
        v = subscript[i];
      }

      // Only one subscript, which means it's something like A[i][i].
      // In this case changing majority won't affect anything.
      if (!inner)
        continue;

      // It used to be (addr + v1 * outer + v2 * inner).
      // After the change, it should be (addr + v2 * outer + v1 * inner).
      builder.setBeforeOp(addr);
      Value v1i = builder.create<IntOp>({ new IntAttr(v1) });
      Value v2i = builder.create<IntOp>({ new IntAttr(v2) });
      Value mul1 = builder.create<MulIOp>({ outer, v2i });
      Value mul2 = builder.create<MulIOp>({ inner, v1i });
      Value add1 = builder.create<AddLOp>({ base, mul1 });
      Op *add2 = builder.create<AddLOp>({ add1, mul2 });

      addr->replaceAllUsesWith(add2);
      todelete.push_back(addr);
    }
  }

  for (auto del : todelete)
    del->erase();
}
