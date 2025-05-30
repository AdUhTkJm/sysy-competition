#include "PrePasses.h"
#include "../opt/Passes.h"

using namespace sys;

#define INT(op) isa<IntOp>(op)

static bool hasStoresTo(Op *op) {
  for (auto use : op->getUses()) {
    // This checks both the case when the address is stored elsewhere,
    // and the value at the address is mutated.
    // (Same for loads.)
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

std::map<std::string, int> EarlyConstFold::stats() {
  return {
    { "folded-ops", foldedTotal }
  };
}

int EarlyConstFold::foldImpl() {
  Builder builder;

  RegularFold fold(module);
  fold.run();
  int folded = fold.stats()["folded-ops"];

  bool changed;
  std::vector<Op*> ifs;
  do {
    changed = false;
    ifs = module->findAll<IfOp>();
    for (auto op : ifs) {
      auto cond = op->getOperand().defining;

      if (!INT(cond))
        continue;

      folded++;
      changed = true;
      auto region = V(cond) ? 0 : 1;
      // Note that the else clause can be empty.
      if (op->getRegions().size() > region) {
        auto ifso = op->getRegion(region);
        for (auto bb : ifso->getBlocks()) {
          auto ops = bb->getOps();
          for (auto inner : ops)
            inner->moveBefore(op);
        }
      }

      // We can't directly use a rewriter, 
      // because this might recursively delete another IfOp.
      op->erase();
      break;
    }
  } while (changed);

  return folded;
}

void EarlyConstFold::run() {
  int folded = 0;

  Builder builder;
  
  // Find all constant globals.
  auto getglobs = module->findAll<GetGlobalOp>();
  auto gMap = getGlobalMap();
  auto fMap = getFunctionMap();
  std::set<GlobalOp*> nonConst;

  for (auto op : getglobs) {
    const auto &name = NAME(op);
    if (hasStoresTo(op)) {
      nonConst.insert(gMap[name]);
      continue;
    }

    for (auto use : op->getUses()) {
      // If we do this before Pureness, we must assume all calls are impure.
      // Moreover, we haven't marked ImpureAttr for CallOps yet, so check the FuncOp instead.
      if (isa<CallOp>(use) && (beforePureness || isExtern(NAME(use)) || fMap[NAME(use)]->has<ImpureAttr>())) {
        nonConst.insert(gMap[name]);
        break;
      }
    }
  }

  do {
    auto allocas = module->findAll<AllocaOp>();
  
    for (auto alloca : allocas) {
      auto uses = alloca->getUses();
      Op *store = nullptr;
      bool good = true;
      for (auto use : uses) {
        if (isa<LoadOp>(use))
          continue;
        
        if (isa<StoreOp>(use)) {
          if (store) {
            good = false;
            break;
          }
          store = use;
          continue;
        }
  
        // This alloca is an array.
        good = false;
        break;
      }
  
      if (!good || !store)
        continue;
  
      // Now this is a constant value.
      Op *def = store->getOperand(0).defining;
  
      // We only propagate compiler-time constants.
      // Other things are better done in Mem2Reg.
      if (!isa<IntOp>(def))
        continue;
      
      for (auto use : uses) {
        if (isa<LoadOp>(use)) {
          use->replaceAllUsesWith(def);
          use->erase();
        }
      }
  
      store->erase();
      alloca->erase();
    }

    // This might make new constants available for allocas to detect.
    // So we put the alloca part in the loop.
    folded = foldImpl();

    // Next we fold access to constant globals.
    getglobs = module->findAll<GetGlobalOp>();
    for (auto get : getglobs) {
      const auto &name = NAME(get);
      auto global = gMap[name];
      if (nonConst.count(global))
        continue;

      auto uses = get->getUses();
      for (auto use : uses) {
        assert(!isa<StoreOp>(use));

        if (isa<LoadOp>(use)) {
          folded++;
          builder.setBeforeOp(use);
          Op *value;
          if (auto intArr = global->find<IntArrayAttr>())
            value = builder.create<IntOp>({ new IntAttr(intArr->vi[0]) });
          else
            value = builder.create<FloatOp>({ new FloatAttr(global->get<FloatArrayAttr>()->vf[0]) });

          use->replaceAllUsesWith(value);
          use->erase();
          continue;
        }

        if (isa<AddLOp>(use)) {
          auto y = use->getOperand(1).defining;
          if (!INT(y))
            continue;

          auto targets = use->getUses();
          for (auto target : targets) {
            assert(!isa<StoreOp>(target));

            auto size = SIZE(global);
            if (V(y) >= size) {
              std::cerr << "warning: out of bounds access\n";
              std::cerr << "array has " << size / 4 << " elements, but accessing subscript " << V(y) / 4 << "\n";
              continue;
            }

            if (isa<LoadOp>(target)) {
              folded++;
              builder.setBeforeOp(use);
              Op *value;
              int offset = V(y) / 4;
              if (auto intArr = global->find<IntArrayAttr>())
                value = builder.create<IntOp>({ new IntAttr(intArr->vi[offset]) });
              else
                value = builder.create<FloatOp>({ new FloatAttr(global->get<FloatArrayAttr>()->vf[offset]) });
              target->replaceAllUsesWith(value);
              target->erase();
              continue;
            }
          }
        }
      }
    }

    // Clean up unused get globals.
    // Don't delay it to DCE, as it will affect Pureness.
    for (auto op : getglobs) {
      if (!op->getUses().size())
        op->erase();
    }

    foldedTotal += folded;
  } while (folded);
}
