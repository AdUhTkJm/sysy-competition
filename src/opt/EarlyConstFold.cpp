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

    // (x + b == i) becomes (x == i - b)
    if (isa<AddIOp>(x) && INT(b)) {
      folded++;
      builder.setBeforeOp(op);
      auto value = builder.create<IntOp>({ new IntAttr(i - V(b)) });
      builder.replace<EqOp>(op, { x, value });
      return true;
    }

    // (x - b == i) becomes (x == i + b)
    if (isa<SubIOp>(x) && INT(b)) {
      folded++;
      builder.setBeforeOp(op);
      auto value = builder.create<IntOp>({ new IntAttr(i + V(b)) });
      builder.replace<EqOp>(op, { x, value });
      return true;
    }

    // (x * b == i) becomes (x == i / b)
    if (isa<SubIOp>(x) && INT(b)) {
      folded++;
      builder.setBeforeOp(op);
      auto value = builder.create<IntOp>({ new IntAttr(i / V(b)) });
      builder.replace<EqOp>(op, { x, value });
      return true;
    }

    // (x / b == i) becomes (x == i * b)
    if (isa<SubIOp>(x) && INT(b)) {
      folded++;
      builder.setBeforeOp(op);
      auto value = builder.create<IntOp>({ new IntAttr(i * V(b)) });
      builder.replace<EqOp>(op, { x, value });
      return true;
    }

    // Note that we can't easily fold (x % b == i).
    return false;
  });

  return folded;
}

void EarlyConstFold::run() {
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

  int folded;
  do {
    folded = foldImpl();
    foldedTotal += folded;
  } while (folded);
}
