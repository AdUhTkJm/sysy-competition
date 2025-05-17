#include "RvAttrs.h"
#include "RvPasses.h"
#include "RvOps.h"

#include "../codegen/Attrs.h"
#include "../codegen/CodeGen.h"

using namespace sys::rv;
using namespace sys;

std::map<std::string, int> InstCombine::stats() {
  return {
    { "combined-instructions", combined }
  };
}

bool inRange(Op *op) {
  auto attr = op->get<IntAttr>();
  return attr->value >= -2048 && attr->value <= 2047;
}

int inRange(int x) {
  return x >= -2048 && x <= 2047;
}

void InstCombine::run() {
  Builder builder;

  runRewriter([&](AddOp *op) {
    auto x = op->getOperand(0).defining;
    auto y = op->getOperand(1).defining;
    if (isa<LiOp>(x) && inRange(x)) {
      combined++;
      builder.replace<AddiOp>(op, { y }, { x->get<IntAttr>() });
      return true;
    }

    if (isa<LiOp>(y) && inRange(y)) {
      combined++;
      builder.replace<AddiOp>(op, { x }, { y->get<IntAttr>() });
      return true;
    }

    return false;
  });

  runRewriter([&](AddwOp *op) {
    auto x = op->getOperand(0).defining;
    auto y = op->getOperand(1).defining;
    if (isa<LiOp>(x) && inRange(x)) {
      combined++;
      builder.replace<AddiwOp>(op, { y }, { x->get<IntAttr>() });
      return true;
    }

    if (isa<LiOp>(y) && inRange(y)) {
      combined++;
      builder.replace<AddiwOp>(op, { x }, { y->get<IntAttr>() });
      return true;
    }

    return false;
  });

  runRewriter([&](SubOp *op) {
    auto x = op->getOperand(0).defining;
    auto y = op->getOperand(1).defining;
    // Note we can't fold on `x`.

    if (isa<LiOp>(y) && inRange(y)) {
      auto val = -V(y);
      if (!inRange(val))
        return false;

      combined++;
      builder.replace<AddiOp>(op, { x }, { new IntAttr(val) });
      return true;
    }

    return false;
  });

  runRewriter([&](SubwOp *op) {
    auto x = op->getOperand(0).defining;
    auto y = op->getOperand(1).defining;
    // Note we can't fold on `x`.

    if (isa<LiOp>(y) && inRange(y)) {
      auto val = -V(y);
      if (!inRange(val))
        return false;

      combined++;
      builder.replace<AddiwOp>(op, { x }, { new IntAttr(val) });
      return true;
    }

    return false;
  });

  runRewriter([&](SllwOp *op) {
    auto x = op->getOperand(0).defining;
    auto y = op->getOperand(1).defining;

    if (isa<LiOp>(y) && inRange(y)) {
      auto val = V(y);
      if (!inRange(val))
        return false;

      combined++;
      builder.replace<SlliwOp>(op, { x }, { new IntAttr(val) });
      return true;
    }

    return false;
  });

  runRewriter([&](SrawOp *op) {
    auto x = op->getOperand(0).defining;
    auto y = op->getOperand(1).defining;

    if (isa<LiOp>(y) && inRange(y)) {
      auto val = V(y);
      if (!inRange(val))
        return false;

      combined++;
      builder.replace<SraiwOp>(op, { x }, { new IntAttr(val) });
      return true;
    }

    return false;
  });

  runRewriter([&](SraOp *op) {
    auto x = op->getOperand(0).defining;
    auto y = op->getOperand(1).defining;

    if (isa<LiOp>(y) && inRange(y)) {
      auto val = V(y);
      if (!inRange(val))
        return false;

      combined++;
      builder.replace<SraiOp>(op, { x }, { new IntAttr(val) });
      return true;
    }

    return false;
  });

  runRewriter([&](AndOp *op) {
    auto x = op->getOperand(0).defining;
    auto y = op->getOperand(1).defining;
    if (isa<LiOp>(x) && inRange(x)) {
      combined++;
      builder.replace<AndiOp>(op, { y }, { x->get<IntAttr>() });
      return true;
    }

    if (isa<LiOp>(y) && inRange(y)) {
      combined++;
      builder.replace<AndiOp>(op, { x }, { y->get<IntAttr>() });
      return true;
    }

    return false;
  });

  runRewriter([&](StoreOp *op) {
    auto value = op->getOperand(0);
    auto addr = op->getOperand(1).defining;
    if (isa<AddiOp>(addr)) {
      auto offset = V(addr);
      auto &currentOffset = V(op);
      if (inRange(offset + currentOffset)) {
        currentOffset += offset;
        auto base = addr->getOperand();
        builder.replace<StoreOp>(op, { value, base }, op->getAttrs());
        return true;
      }
    }
    return false;
  });

  runRewriter([&](LoadOp *op) {
    auto addr = op->getOperand(0).defining;
    if (isa<AddiOp>(addr)) {
      auto offset = V(addr);
      auto &currentOffset = V(op);
      if (inRange(offset + currentOffset)) {
        currentOffset += offset;
        auto base = addr->getOperand();
        builder.replace<LoadOp>(op, op->getResultType(), { base }, op->getAttrs());
        return true;
      }
    }
    return false;
  });

  runRewriter([&](AddiwOp *op) {
    if (V(op) == 0) {
      op->replaceAllUsesWith(op->getOperand().defining);
      op->erase();
      return true;
    }
    return false;
  });

  runRewriter([&](AddiOp *op) {
    if (V(op) == 0) {
      op->replaceAllUsesWith(op->getOperand().defining);
      op->erase();
      return true;
    }
    return false;
  });

  runRewriter([&](PhiOp *op) {
    // Remove phi with a single operand.
    if (op->getOperands().size() == 1) {
      auto def = op->getOperand().defining;
      op->replaceAllUsesWith(def);
      op->erase();
      return true;
    }
    return false;
  });

  runRewriter([&](BeqOp *op) {
    auto def = op->DEF(0);
    auto zero = op->DEF(1);
    // Replace `beq (seqz x), zero` with `bne x, zero`.
    if (isa<SeqzOp>(def) && isa<ReadRegOp>(zero) && REG(zero) == Reg::zero) {
      builder.replace<BneOp>(op, { def->DEF(0), zero }, op->getAttrs());
      return true;
    }
    return false;
  });
  
  // Similarly `bne (seqz x), zero` becomes `beq x, zero`.
  runRewriter([&](BneOp *op) {
    auto def = op->DEF(0);
    auto zero = op->DEF(1);
    if (isa<SeqzOp>(def) && isa<ReadRegOp>(zero) && REG(zero) == Reg::zero) {
      builder.replace<BeqOp>(op, { def->DEF(0), zero }, op->getAttrs());
      return true;
    }
    return false;
  });
  
  // Only run this after all int-related fold completes.
  // Rewrite `li a0, 0` into reading from `zero`.
  runRewriter([&](LiOp *op) {
    if (V(op) == 0)
      builder.replace<ReadRegOp>(op, Value::i32, { new RegAttr(Reg::zero)} );
    
    return false;
  });
}
