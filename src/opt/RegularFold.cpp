#include "Passes.h"

using namespace sys;

#define INT(op) isa<IntOp>(op)

std::map<std::string, int> RegularFold::stats() {
  return {
    { "folded-ops", foldedTotal }
  };
}

int RegularFold::foldImpl() {
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

    // (x + 0) becomes (x)
    if (INT(y) && V(y) == 0) {
      folded++;
      op->replaceAllUsesWith(x);
      op->erase();
      return true;
    }

    // ((a + B) + Y) becomes (a + (B + Y))
    if (isa<AddIOp>(x) && INT(y)) {
      auto a = x->getOperand(0).defining;
      auto b = x->getOperand(1).defining;
      if (INT(b)) {
        folded++;
        builder.setBeforeOp(op);
        auto imm = builder.create<IntOp>({ new IntAttr(V(b) + V(y)) });
        builder.replace<AddIOp>(op, { a, imm });
        return true;
      }
    }

    // ((a / C) + (b / C)) becomes ((a + b) / C)
    if (isa<DivIOp>(x) && isa<DivIOp>(y)) {
      Value a = x->getOperand(0).defining;
      auto c = x->getOperand(1).defining;
      Value b = y->getOperand(0).defining;
      auto d = y->getOperand(1).defining;
      if (INT(c) && INT(d) && V(c) == V(d)) {
        folded++;
        builder.setBeforeOp(op);
        auto addi = builder.create<AddIOp>({ a, b });
        builder.replace<DivIOp>(op, { addi, c });
        return true;
      }
    }

    // ((a * C) + (b * C)) becomes ((a + b) * C)
    if (isa<MulIOp>(x) && isa<MulIOp>(y)) {
      Value a = x->getOperand(0).defining;
      auto c = x->getOperand(1).defining;
      Value b = y->getOperand(0).defining;
      auto d = y->getOperand(1).defining;
      if (INT(c) && INT(d) && V(c) == V(d)) {
        folded++;
        builder.setBeforeOp(op);
        auto addi = builder.create<AddIOp>({ a, b });
        builder.replace<MulIOp>(op, { addi, c });
        return true;
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

    // (x + 0) becomes (x)
    if (INT(y) && V(y) == 0) {
      folded++;
      op->replaceAllUsesWith(x);
      op->erase();
      return true;
    }
    
    // ((a + B) + Y) becomes (a + (B + Y))
    if (isa<AddLOp>(x) && INT(y)) {
      auto a = x->getOperand(0).defining;
      auto b = x->getOperand(1).defining;
      if (INT(b)) {
        folded++;
        builder.setBeforeOp(op);
        auto imm = builder.create<IntOp>({ new IntAttr(V(b) + V(y)) });
        builder.replace<AddLOp>(op, { a, imm });
        return true;
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
    
    // (x - 0) becomes (x)
    if (INT(y) && V(y) == 0) {
      folded++;
      op->replaceAllUsesWith(x);
      op->erase();
      return true;
    }

    // (x - x) becomes 0
    if (x == y) {
      folded++;
      builder.replace<IntOp>(op, { new IntAttr(0) });
      return true;
    }

    // ((a + A) - B) becomes (a + (A - B))
    if (isa<AddIOp>(x) && INT(y)) {
      auto a = x->getOperand(0).defining;
      auto b = x->getOperand(1).defining;
      if (INT(b)) {
        folded++;
        builder.setBeforeOp(op);
        auto imm = builder.create<IntOp>({ new IntAttr(V(b) - V(y)) });
        builder.replace<AddIOp>(op, { a, imm });
        return true;
      }
    }

    // ((a + b) - b) becomes (a)
    if (isa<AddIOp>(x)) {
      auto a = x->getOperand(0).defining;
      auto b = x->getOperand(1).defining;

      if (b == y) {
        folded++;
        op->replaceAllUsesWith(a);
        op->erase();
        return true;
      }
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

  runRewriter([&](DivIOp *op) {
    auto x = op->getOperand(0).defining;
    auto y = op->getOperand(1).defining;

    if (INT(x) && INT(y)) {
      folded++;
      builder.replace<IntOp>(op, { new IntAttr(V(x) / V(y)) });
      return true;
    }

    // (x / 1) becomes x
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

    if (!INT(y)) {
      if (!INT(x))
        return false;

      auto i = V(x);
      if (y->getOperands().size() < 2)
        return false;
  
      auto a = y->getOperand(0).defining;
      auto b = y->getOperand(1).defining;

      // (I < a + B) becomes (I - B < a)
      if (isa<AddIOp>(y) && INT(b)) {
        folded++;
        builder.setBeforeOp(op);
        auto value = builder.create<IntOp>({ new IntAttr(i - V(b)) });
        builder.replace<LtOp>(op, { value, a });
        return true;
      }

      // (I < a - B) becomes (I + B < a)
      if (isa<SubIOp>(y) && INT(b)) {
        folded++;
        builder.setBeforeOp(op);
        auto value = builder.create<IntOp>({ new IntAttr(i + V(b)) });
        builder.replace<LtOp>(op, { value, a });
        return true;
      }

      // (I < A - b) becomes (b < A - I)
      if (isa<SubIOp>(y) && INT(a)) {
        folded++;
        builder.setBeforeOp(op);
        auto value = builder.create<IntOp>({ new IntAttr(V(a) - i) });
        builder.replace<LtOp>(op, { b, value });
        return true;
      }

      // (I < a * B) becomes (I / B < a) if B > 0, I >= 0
      if (isa<MulIOp>(y) && INT(b) && V(b) > 0 && i >= 0) {
        folded++;
        builder.setBeforeOp(op);
        auto value = builder.create<IntOp>({ new IntAttr((i - 1) / V(b)) });
        builder.replace<LtOp>(op, { value, a });
        return true;
      }
      
      // (I < a / B) becomes ((I + 1) * B - 1 < a) if B > 0, I >= 0
      // Note:
      //   a / 3 < 7 => a < 21
      //   a / 3 > 7 => a >= 24
      if (isa<DivIOp>(y) && INT(b) && V(b) > 0 && i >= 0) {
        folded++;
        builder.setBeforeOp(op);
        auto value = builder.create<IntOp>({ new IntAttr((i + 1) * V(b) - 1) });
        builder.replace<LtOp>(op, { value, a });
        return true;
      }

      // (I < A / b) CANNOT become ((b < A / I) && (b > 0)) if A >= 0, I > 0
      // Eg. (18 / b > 3) should become (b < 5) rather than (b < 6)

      // (I < a % B) becomes 0 if (i >= b > 0).
      // Works even if a < 0.
      if (isa<ModIOp>(y) && INT(b) && i >= V(b) && V(b) > 0) {
        folded++;
        builder.replace<IntOp>(op, { new IntAttr(0) });
        return true;
      }

      return false;
    }

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

    // (A - b < I) becomes (A - I < b)
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
      auto value = builder.create<IntOp>({ new IntAttr(i / V(b)) });
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

    // (A / b < I) becomes ((A / I < b) || b < 0) if A >= 0, I > 0
    if (isa<DivIOp>(x) && INT(a) && V(a) >= 0 && i > 0) {
      folded++;
      builder.setBeforeOp(op);
      auto value = builder.create<IntOp>({ new IntAttr(V(a) / i) });
      Value lval = builder.create<LtOp>({ value, b });
      auto zero = builder.create<IntOp>({ new IntAttr(0) });
      Value ltz = builder.create<LtOp>({ b, zero });
      builder.replace<OrIOp>(op, { lval, ltz });
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

  runRewriter([&](LeOp *op) {
    auto x = op->getOperand(0).defining;
    auto y = op->getOperand(1).defining;

    if (INT(x) && INT(y)) {
      folded++;
      builder.replace<IntOp>(op, { new IntAttr(V(x) <= V(y)) });
      return true;
    }

    builder.setBeforeOp(op);

    // (X <= y) becomes (X - 1 < y)
    if (INT(x)) {
      folded++;
      auto xm1 = builder.create<IntOp>({ new IntAttr(V(x) - 1) });
      builder.replace<LtOp>(op, { xm1, y });
      return true;
    }

    // (x <= Y) becomes (x < Y + 1)
    if (INT(y)) {
      folded++;
      auto yp1 = builder.create<IntOp>({ new IntAttr(V(y) + 1) });
      builder.replace<LtOp>(op, { x, yp1 });
      return true;
    }

    return false;
  });

  return folded;
}

void RegularFold::run() {
  foldedTotal = foldImpl();
}
