#include "Passes.h"

using namespace sys;

#define V(op) (op)->get<IntAttr>()->value
#define INT(op) isa<IntOp>(op)

// Defined in Pureness.cpp.
bool hasStoresTo(Op *op);

int EarlyConstFold::foldImpl() {
  Builder builder;

  int folded = 0;

  runRewriter([&](AddIOp *op) {
    auto x = op->getOperand(0).defining;
    auto y = op->getOperand(1).defining;

    if (INT(x) && INT(y)) {
      folded++;
      builder.replace<IntOp>(op, { new IntAttr(V(x) + V(y)) });
      return true;
    }
    
    // Canonicalize.
    if (INT(x) && !INT(y)) {
      builder.replace<AddIOp>(op, { y, x }, op->getAttrs());
      return true;
    }

    // ((a + B) + Y) becomes (a + (B + Y))
    if (isa<AddIOp>(x) && INT(y)) {
      auto a = x->getOperand(0).defining;
      auto b = x->getOperand(1).defining;
      if (INT(b)) {
        builder.setBeforeOp(op);
        auto imm = builder.create<IntOp>({ new IntAttr(V(b) + V(y)) });
        builder.replace<AddIOp>(op, { a, imm });
      }
    }

    return false;
  });

  // Similar to AddI, but no direct const folding is possible.
  runRewriter([&](AddLOp *op) {
    auto x = op->getOperand(0).defining;
    auto y = op->getOperand(1).defining;

    // Canonicalize.
    if (INT(x) && !INT(y)) {
      builder.replace<AddIOp>(op, { y, x }, op->getAttrs());
      return true;
    }

    // ((a + B) + Y) becomes (a + (B + Y))
    if (isa<AddLOp>(x) && INT(y)) {
      auto a = x->getOperand(0).defining;
      auto b = x->getOperand(1).defining;
      if (INT(b)) {
        builder.setBeforeOp(op);
        auto imm = builder.create<IntOp>({ new IntAttr(V(b) + V(y)) });
        builder.replace<AddLOp>(op, { a, imm });
      }
    }

    return false;
  });

  runRewriter([&](SubIOp *op) {
    auto x = op->getOperand(0).defining;
    auto y = op->getOperand(1).defining;

    if (INT(x) && INT(y)) {
      folded++;
      builder.replace<IntOp>(op, { new IntAttr(V(x) - V(y)) });
      return true;
    }
    return false;
  });

  runRewriter([&](MulIOp *op) {
    auto x = op->getOperand(0).defining;
    auto y = op->getOperand(1).defining;

    if (INT(x) && INT(y)) {
      folded++;
      builder.replace<IntOp>(op, { new IntAttr(V(x) * V(y)) });
      return true;
    }

    // Canonicalize.
    if (INT(x) && !INT(y)) {
      builder.replace<MulIOp>(op, { y, x }, op->getAttrs());
      return true;
    }
    
    return false;
  });

  runRewriter([&](DivIOp *op) {
    auto x = op->getOperand(0).defining;
    auto y = op->getOperand(1).defining;

    if (INT(x) && INT(y)) {
      folded++;
      builder.replace<IntOp>(op, { new IntAttr(V(x) / V(y)) });
      return true;
    }

    // (x * 0) becomes 0
    if (INT(y) && V(y) == 0) {
      folded++;
      builder.replace<IntOp>(op, { new IntAttr(0) });
      return true;
    }

    // (x * 1) becomes x
    if (INT(y) && V(y) == 1) {
      folded++;
      op->replaceAllUsesWith(x);
      op->erase();
      return true;
    }

    return false;
  });

  runRewriter([&](MinusOp *op) {
    auto x = op->getOperand().defining;

    if (INT(x)) {
      folded++;
      builder.replace<IntOp>(op, { new IntAttr(-V(x)) });
      return true;
    }

    return false;
  });

  runRewriter([&](ModIOp *op) {
    auto x = op->getOperand(0).defining;
    auto y = op->getOperand(1).defining;

    if (INT(x) && INT(y)) {
      folded++;
      builder.replace<IntOp>(op, { new IntAttr(V(x) % V(y)) });
      return true;
    }

    // (x % 1) becomes 0
    if (INT(y) && V(y) == 1) {
      folded++;
      builder.replace<IntOp>(op, { new IntAttr(0) });
      return true;
    }

    return false;
  });

  runRewriter([&](EqOp *op) {
    auto x = op->getOperand(0).defining;
    auto y = op->getOperand(1).defining;

    if (INT(x) && INT(y)) {
      folded++;
      builder.replace<IntOp>(op, { new IntAttr(V(x) == V(y)) });
      return true;
    }

    // Canonicalize.
    if (INT(x) && !INT(y)) {
      builder.replace<EqOp>(op, { y, x }, op->getAttrs());
      return true;
    }

    if (!INT(y))
      return false;

    auto i = V(y);

    if (x->getOperands().size() < 2)
      return false;

    auto a = x->getOperand(0).defining;
    auto b = x->getOperand(1).defining;

    // (a + B == I) becomes (a == I - B)
    if (isa<AddIOp>(x) && INT(b)) {
      folded++;
      builder.setBeforeOp(op);
      auto value = builder.create<IntOp>({ new IntAttr(i - V(b)) });
      builder.replace<EqOp>(op, { a, value });
      return true;
    }

    // (a - B == I) becomes (a == I + B)
    if (isa<SubIOp>(x) && INT(b)) {
      folded++;
      builder.setBeforeOp(op);
      auto value = builder.create<IntOp>({ new IntAttr(i + V(b)) });
      builder.replace<EqOp>(op, { a, value });
      return true;
    }

    // (A - b == I) becomes (b == A - I)
    if (isa<SubIOp>(x) && INT(a)) {
      folded++;
      builder.setBeforeOp(op);
      auto value = builder.create<IntOp>({ new IntAttr(V(a) - i) });
      builder.replace<EqOp>(op, { b, value });
      return true;
    }

    // (a * B == I) becomes (a == I / B), if (I % B == 0)
    if (isa<MulIOp>(x) && INT(b) && i % V(b) == 0) {
      folded++;
      builder.setBeforeOp(op);
      auto value = builder.create<IntOp>({ new IntAttr(i / V(b)) });
      builder.replace<EqOp>(op, { a, value });
      return true;
    }

    // (a * B == I) becomes 0, if (I % B != 0)
    if (isa<MulIOp>(x) && INT(b) && i % V(b) != 0) {
      folded++;
      builder.replace<IntOp>(op, { new IntAttr(0) });
      return true;
    }

    // (a / B == I) becomes (a < (I + 1) * B) && (a >= I * B), if (b > 0)
    if (isa<DivIOp>(x) && INT(b) && V(b) > 0) {
      folded++;
      builder.setBeforeOp(op);
      auto lowerBound = builder.create<IntOp>({ new IntAttr(i * V(b)) });
      auto upperBound = builder.create<IntOp>({ new IntAttr((i + 1) * V(b)) });
      auto comp1 = builder.create<LtOp>({ a, upperBound });
      auto comp2 = builder.create<LeOp>({ lowerBound, a });
      builder.replace<AndIOp>(op, { comp1, comp2 });
      return true;
    }

    // Note that (b / a == i) doesn't necessarily mean (a == b / i).
    // For example 255 / 16 == 15 but 255 / 17 is also 15.
    // Not sure how to find bounds. Is the range (b / (i + 1), b / i] correct?

    // We can't easily fold (a % b == i).
    return false;
  });

  runRewriter([&](LtOp *op) {
    auto x = op->getOperand(0).defining;
    auto y = op->getOperand(1).defining;

    if (INT(x) && INT(y)) {
      folded++;
      builder.replace<IntOp>(op, { new IntAttr(V(x) < V(y)) });
      return true;
    }

    // TODO: also handle (const < x)

    if (!INT(y))
      return false;

    auto i = V(y);

    if (x->getOperands().size() < 2)
      return false;

    auto a = x->getOperand(0).defining;
    auto b = x->getOperand(1).defining;

    // (a + B < I) becomes (a < I - B)
    if (isa<AddIOp>(x) && INT(b)) {
      folded++;
      builder.setBeforeOp(op);
      auto value = builder.create<IntOp>({ new IntAttr(i - V(b)) });
      builder.replace<LtOp>(op, { a, value });
      return true;
    }

    // (a - B < I) becomes (a < I + B)
    if (isa<SubIOp>(x) && INT(b)) {
      folded++;
      builder.setBeforeOp(op);
      auto value = builder.create<IntOp>({ new IntAttr(i + V(b)) });
      builder.replace<LtOp>(op, { a, value });
      return true;
    }

    // (a - B < I) becomes (a - I < B)
    if (isa<SubIOp>(x) && INT(a)) {
      folded++;
      builder.setBeforeOp(op);
      auto value = builder.create<IntOp>({ new IntAttr(V(a) - i) });
      builder.replace<LtOp>(op, { value, b });
      return true;
    }

    // (a * B < I) becomes (a < I / B) if B > 0, I >= 0
    if (isa<MulIOp>(x) && INT(b) && V(b) > 0 && i >= 0) {
      folded++;
      builder.setBeforeOp(op);
      auto value = builder.create<IntOp>({ new IntAttr((i - 1) / V(b)) });
      builder.replace<LtOp>(op, { a, value });
      return true;
    }
    
    // (a / B < I) becomes (a < I * B) if B > 0, I >= 0
    if (isa<DivIOp>(x) && INT(b) && V(b) > 0 && i >= 0) {
      folded++;
      builder.setBeforeOp(op);
      auto value = builder.create<IntOp>({ new IntAttr(i * V(b)) });
      builder.replace<LtOp>(op, { a, value });
      return true;
    }

    // (A / b < I) becomes (A / I < b) if A >= 0, I > 0
    if (isa<DivIOp>(x) && INT(a) && V(a) >= 0 && i > 0) {
      folded++;
      builder.setBeforeOp(op);
      auto value = builder.create<IntOp>({ new IntAttr(V(a) / i) });
      builder.replace<LtOp>(op, { value, b });
      return true;
    }

    // (a % B < I) becomes 1 if (i >= b > 0).
    if (isa<ModIOp>(x) && INT(b) && i >= V(b) && V(b) > 0) {
      folded++;
      builder.replace<IntOp>(op, { new IntAttr(1) });
      return true;
    }

    return false;
  });

  bool changed;
  std::vector<IfOp*> ifs;
  do {
    changed = false;
    ifs = module->findAll<IfOp>();
    for (auto op : ifs) {
      auto cond = op->getOperand().defining;

      if (!INT(cond))
        continue;

      folded++;
      changed = true;
      auto ifso = op->getRegion(V(cond) ? 0 : 1);
      for (auto bb : ifso->getBlocks()) {
        auto ops = bb->getOps();
        for (auto inner : ops)
          inner->moveBefore(op);
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
  std::set<GlobalOp*> nonConst;

  for (auto get : getglobs) {
    if (hasStoresTo(get)) {
      const auto &name = get->get<NameAttr>()->name;
      nonConst.insert(gMap[name]);
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
    // We do not erase any GetGlobalOp, so it's safe to reuse the vector.
    for (auto get : getglobs) {
      const auto &name = get->get<NameAttr>()->name;
      auto global = gMap[name];
      if (nonConst.count(global))
        continue;

      auto uses = get->getUses();
      for (auto use : uses) {
        assert(!isa<StoreOp>(use));

        if (isa<LoadOp>(use)) {
          folded++;
          builder.setBeforeOp(use);
          auto value = builder.create<IntOp>({ new IntAttr(global->get<IntArrayAttr>()->vi[0]) });
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

            auto size = global->get<SizeAttr>()->value;
            if (V(y) >= size) {
              std::cerr << "warning: out of bounds access\n";
              std::cerr << "array has " << size / 4 << " elements, but accessing subscript " << V(y) / 4 << "\n";
              continue;
            }

            if (isa<LoadOp>(target)) {
              folded++;
              builder.setBeforeOp(use);
              auto value = builder.create<IntOp>({ new IntAttr(global->get<IntArrayAttr>()->vi[V(y) / 4]) });
              target->replaceAllUsesWith(value);
              target->erase();
              continue;
            }
          }
        }
      }
    }

    foldedTotal += folded;
  } while (folded);
}
