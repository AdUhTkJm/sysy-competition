#include "Passes.h"

using namespace sys;

#define V(op) (op)->get<IntAttr>()->value
#define INT(op) isa<IntOp>(op)

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

    // Canonicalize.
    if (INT(x) && !INT(y)) {
      builder.replace<MulIOp>(op, { y, x }, op->getAttrs());
      return true;
    }

    // x * 0 == 0
    if (INT(y) && V(y) == 0) {
      folded++;
      builder.replace<IntOp>(op, { new IntAttr(0) });
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

    // (a + b == i) becomes (a == i - b)
    if (isa<AddIOp>(x) && INT(b)) {
      folded++;
      builder.setBeforeOp(op);
      auto value = builder.create<IntOp>({ new IntAttr(i - V(b)) });
      builder.replace<EqOp>(op, { a, value });
      return true;
    }

    // (a - b == i) becomes (a == i + b)
    if (isa<SubIOp>(x) && INT(b)) {
      folded++;
      builder.setBeforeOp(op);
      auto value = builder.create<IntOp>({ new IntAttr(i + V(b)) });
      builder.replace<EqOp>(op, { a, value });
      return true;
    }

    // (a - b == i) becomes (b == a - i)
    if (isa<SubIOp>(x) && INT(a)) {
      folded++;
      builder.setBeforeOp(op);
      auto value = builder.create<IntOp>({ new IntAttr(V(a) - i) });
      builder.replace<EqOp>(op, { b, value });
      return true;
    }

    // (a * b == i) becomes (a == i / b)
    if (isa<MulIOp>(x) && INT(b)) {
      folded++;
      builder.setBeforeOp(op);
      auto value = builder.create<IntOp>({ new IntAttr(i / V(b)) });
      builder.replace<EqOp>(op, { a, value });
      return true;
    }

    // (a / b == i) becomes (a == i * b)
    if (isa<DivIOp>(x) && INT(b)) {
      folded++;
      builder.setBeforeOp(op);
      auto value = builder.create<IntOp>({ new IntAttr(i * V(b)) });
      builder.replace<EqOp>(op, { a, value });
      return true;
    }

    // Note that (b / x == i) doesn't necessarily mean (x == b / i).
    // For example 255 / 16 == 15 but 255 / 17 is also 15.

    // Note that we can't easily fold (x % b == i).
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

    // (a + b < i) becomes (a < i - b)
    if (isa<AddIOp>(x) && INT(b)) {
      folded++;
      builder.setBeforeOp(op);
      auto value = builder.create<IntOp>({ new IntAttr(i - V(b)) });
      builder.replace<LtOp>(op, { a, value });
      return true;
    }

    // (a - b < i) becomes (a < i + b)
    if (isa<SubIOp>(x) && INT(b)) {
      folded++;
      builder.setBeforeOp(op);
      auto value = builder.create<IntOp>({ new IntAttr(i + V(b)) });
      builder.replace<LtOp>(op, { a, value });
      return true;
    }

    // (a - b < i) becomes (a - i < b)
    if (isa<SubIOp>(x) && INT(a)) {
      folded++;
      builder.setBeforeOp(op);
      auto value = builder.create<IntOp>({ new IntAttr(V(a) - i) });
      builder.replace<LtOp>(op, { value, b });
      return true;
    }

    // (a * b < i) becomes (a < i / b) if b > 0, i >= 0
    if (isa<MulIOp>(x) && INT(b) && V(b) > 0 && i >= 0) {
      folded++;
      builder.setBeforeOp(op);
      auto value = builder.create<IntOp>({ new IntAttr((i - 1) / V(b)) });
      builder.replace<LtOp>(op, { a, value });
      return true;
    }
    
    // (a / b < i) becomes (a < i * b) if b > 0, i >= 0
    if (isa<DivIOp>(x) && INT(b) && V(b) > 0 && i >= 0) {
      folded++;
      builder.setBeforeOp(op);
      auto value = builder.create<IntOp>({ new IntAttr(i * V(b)) });
      builder.replace<LtOp>(op, { a, value });
      return true;
    }

    // (a / b < i) becomes (a / i < b) if a >= 0, i > 0
    if (isa<DivIOp>(x) && INT(a) && V(a) >= 0 && i > 0) {
      folded++;
      builder.setBeforeOp(op);
      auto value = builder.create<IntOp>({ new IntAttr(V(a) / i) });
      builder.replace<LtOp>(op, { value, b });
      return true;
    }

    // Note that we can't easily fold (x % b == i).
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
      // because this might recursively delete other IfOp.
      op->erase();
      break;
    }
  } while (changed);

  return folded;
}

void EarlyConstFold::run() {
  int folded;
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
    foldedTotal += folded;
  } while (folded);
}
