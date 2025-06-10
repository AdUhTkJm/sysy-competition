#include "PreLoopPasses.h"
#include "PreAnalysis.h"

using namespace sys;

std::map<std::string, int> View::stats() {
  return {
    { "inlined-views", inlined },
  };
}

namespace {

// Calculate the permutation that changes from `from` to `to`. (Bad wording.)
std::vector<int> perm(const std::vector<int> &from, const std::vector<int> &to) {
  // Maps value to indices.
  std::unordered_map<int, std::vector<int>> v2i;
  for (int i = 0; i < from.size(); ++i)
    v2i[from[i]].push_back(i);

  std::vector<int> result;
  for (int val : to) {
    result.push_back(v2i[val].back());
    v2i[val].pop_back();
  }
  return result;
}

// Applys permutation `perm` to `data`, but only for the non-zero values.
std::vector<int> apply(const std::vector<int> &data, const std::vector<int> &perm) {
  auto result = data;
  // Indices of non-zero values.
  std::vector<int> nz;
  for (int i = 0; i < data.size(); ++i) {
    if (data[i])
      nz.push_back(i);
  }

  // Extract the non-zero values.
  std::vector<int> nonzeros;
  for (auto i : nz)
    nonzeros.push_back(data[i]);

  // Apply the permutation.
  std::vector<int> permuted(nonzeros.size());
  for (int i = 0; i < perm.size(); ++i)
    permuted[i] = nonzeros[perm[i]];

  // Write back the permuted values to their original positions.
  for (int i = 0; i < nz.size(); ++i)
    result[nz[i]] = permuted[i];
  return result;
}

// Check whether `from` has been written to since `start`.
bool writeless(Op *start, Op *from) {
  for (auto runner = start; !runner->atBack();) {
    runner = runner->nextOp();
    if (isa<StoreOp>(runner) && BASE(runner->DEF(1)) == BASE(from))
      return false;
    // Also check for impurities.
    if (isa<CallOp>(runner) && runner->has<ImpureAttr>()) {
      for (auto operand : runner->getOperands()) {
        auto def = operand.defining;
        if (def->has<BaseAttr>() && BASE(def) == BASE(from))
          return false;
      }
    }
  }
  // Check outer regions.
  if (auto parent = start->getParentOp(); !isa<FuncOp>(parent))
    return writeless(parent, from);
  return true;
}

}

// Identify memory views.
// In MLIR, the equivalent would be to eliminate the `memref.reinterpret`
// that views `memref.alloca` as different shapes.
// We can do it as long as we've got subscript information.
void View::runImpl(Op *func) {
  // First, find out view candidates.
  std::unordered_map<Op*, int> bases;

  if (func->has<AtMostOnceAttr>()) {
    auto gets = func->findAll<GetGlobalOp>();
    for (auto get : gets) {
      // If the global variable is used elsewhere,
      // we cannot touch it.
      if (usedIn[NAME(get)].size() <= 1)
        bases[get] = 2;
    }
  }
  
  // Put allocas in `bases`.
  auto region = func->getRegion();
  auto entry = region->getFirstBlock();
  if (entry->getOpCount() && isa<AllocaOp>(entry->getFirstOp())) {
    for (auto op : entry->getOps())
      bases[op] = 2;
  }

  // Find all stores. A view should only be stored to once.
  auto stores = func->findAll<StoreOp>();
  for (auto store : stores) {
    auto addr = store->DEF(1);
    if (!addr->has<BaseAttr>())
      return;

    auto base = BASE(addr);

    // Stored more than once.
    if (bases.count(base) && --bases[base] <= 0)
      bases.erase(base);
  }

  // The remaining things are const arrays or views.
  auto loads = func->findAll<LoadOp>();
  for (auto load : loads) {
    if (!load->DEF()->has<BaseAttr>())
      return;
  }

  for (auto [k, v] : bases) {
    // Const arrays. Superopt will account for this.
    if (v == 2)
      continue;

    // If this is a view of another array,
    // then the store must be exactly a load from that array.
    Op *store = nullptr;
    for (auto s : stores) {
      auto base = BASE(s->DEF(1));
      if (base == k) {
        store = s;
        break;
      }
    }
    auto addr = store->DEF(1);
    if (!addr->has<SubscriptAttr>())
      continue;

    auto val = store->DEF(0);
    if (!isa<LoadOp>(val))
      continue;

    auto from = val->DEF();
    if (!from->has<SubscriptAttr>() || addr->getParent() != from->getParent())
      continue;

    // From shouldn't have been written to since this view is created.
    if (!writeless(store, from) || !writeless(store, addr))
      continue;

    // All subscripts must have the same size.
    // (This should have been guaranteed by CodeGen, but I'm not 100% sure.)
    auto sa = SUBSCRIPT(addr);
    auto sf = SUBSCRIPT(from);
    if (sa.size() != sf.size())
      continue;

    // Constants should be dealt with carefully.
    int ca = sa.back(), cf = sf.back();
    sa.pop_back();
    sf.pop_back();
  
    // Find out whether `sa` is a permutation of `sf`.
    // More general cases might involve linear algebra.
    // Though it's acceptable, it might introduce fractions, which are hard to reason about.
    // So we restrict it to permutation.
    if (!std::is_permutation(sa.begin(), sa.end(), sf.begin()))
      continue;

    auto perm = ::perm(sa, sf);

    bool good = true;
    for (auto load : loads) {
      auto ld = load->DEF();
      if (BASE(ld) != k)
        continue;

      if (!ld->has<SubscriptAttr>()) {
        good = false;
        break;
      }

      auto sub = SUBSCRIPT(ld);
      // The number of non-zero non-constant elements must be the same as `sa.size()`.
      int v = sa.size();
      for (int i = 0; i < sub.size() - 1; i++) {
        if (sub[i])
          v--;
      }
      if (v) {
        good = false;
        break;
      }
    }
    if (!good)
      continue;

    // Now we're sure that `addr` is a view of `from`.
    // Replace all its uses to the use of `from` instead.
    inlined++;

    store->erase();

    Builder builder;
    for (auto load : loads) {
      auto ld = load->DEF();
      if (!ld->has<BaseAttr>() || !ld->has<SubscriptAttr>())
        continue;
      if (BASE(ld) != k)
        continue;

      auto sub = ::apply(SUBSCRIPT(ld), perm);

      // Re-synthesize it.
      builder.setBeforeOp(load);

      // First find all outer loops.
      std::vector<Op*> outer;
      Op *x = ld;
      for (int i = 0; i < sub.size() - 1; i++) {
        x = x->getParentOp<ForOp>();
        outer.push_back(x);
      }
      std::reverse(outer.begin(), outer.end());

      auto base = BASE(from);
      Op *addr = base;
      for (int i = 0; i < sub.size() - 1; i++) {
        Value vi = builder.create<IntOp>({ new IntAttr(sub[i]) });
        Value mul = builder.create<MulIOp>({ outer[i], vi });
        addr = builder.create<AddLOp>({ addr, mul });
        addr->add<BaseAttr>(base);
      }

      // Add the constant.
      // Suppose ca = 5, cf = 3 and sub.back() = 9.
      // Then this is saying a[...+9], which should be f[...+7].
      // So it is sub.back() - (ca - cf).
      int vi = sub.back() - (ca - cf);
      if (vi) {
        Value li = builder.create<IntOp>({ new IntAttr(vi) });
        addr = builder.create<AddLOp>({ addr, li });
        addr->add<BaseAttr>(base);
      }

      load->setOperand(0, addr);
    }
  }
}

void View::run() {
  ArrayAccess(module).run();
  Base(module).run();

  auto funcs = collectFuncs();

  // Analyze global variable usage.
  for (auto func : funcs) {
    auto gets = func->findAll<GetGlobalOp>();
    for (auto get : gets)
      usedIn[NAME(get)].insert(func);
  }

  for (auto func : funcs)
    runImpl(func);
}
