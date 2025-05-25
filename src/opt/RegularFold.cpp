#include "Passes.h"
#include "../utils/Matcher.h"

using namespace sys;

#define INT(op) isa<IntOp>(op)

static Rule rules[] = {
  // Addition
  "(change (add x 0) x)",
  "(change (add 'a 'b) (!add 'a 'b))",
  "(change (add 'a x) (add x 'a))",
  "(change (add x (minus y)) (sub x y))",
  "(change (add (minus x) y) (sub y x))",
  "(change (add (add x 'a) 'b) (add x (!add 'a 'b)))",
  "(change (add (mul x 'a) (mul x 'b)) (mul x (!add 'a 'b)))",
  "(change (add (mul x 'a) x) (mul (!add 'a 1) x))",
  "(change (add x (mul x 'a)) (mul (!add 'a 1) x))",
  "(change (add (mul x 'a) (mul y 'a)) (mul (add x y) 'a))",
  "(change (add (div 'a x) (div 'b x)) (div (!add 'a 'b) x))",
  "(change (add (div x 'a) (div y 'a)) (div (add x y) 'a))",
  "(change (add (add x x) x) (mul x 3))",
  "(change (add x (add x x)) (mul x 3))",

  // Addition (64-bit)
  "(change (addl x 0) x)",
  "(change (addl 'a x) (addl x 'a))",
  "(change (addl (addl x 'a) 'b) (addl x (!add 'a 'b)))",

  // FP Addition
  "(change (fadd *a *b) (?add *a *b))",
  "(change (fadd x *0) x)",
  "(change (fadd *a x) (fadd x *a))",
  "(change (fadd x (fminus y)) (fsub x y))",
  "(change (fadd (fminus x) y) (fsub y x))",

  // Subtraction
  "(change (sub x 0) x)",
  "(change (sub x x) 0)",
  "(change (sub 'a 'b) (!sub 'a 'b))",
  "(change (sub (add x y) x) y)",
  "(change (sub (add x y) y) x)",
  "(change (sub (add x 'a) 'b) (add x (!sub 'a 'b)))",
  "(change (sub (sub x 'a) 'b) (sub x (!add 'a 'b)))",
  "(change (sub x (minus y)) (add x y))",
  "(change (sub (mul x 'a) (mul x 'b)) (mul x (!sub 'a 'b)))",
  "(change (add (mul x 'a) x) (mul (!sub 'a 1) x))",
  "(change (sub (mul x 'a) (mul y 'a)) (mul (sub x y) 'a))",
  "(change (sub (div 'a x) (div 'b x)) (div (!sub 'a 'b) x))",
  "(change (sub (div x 'a) (div y 'a)) (div (sub x y) 'a))",

  // FP subtraction
  "(change (fsub *a *b) (?sub *a *b))",
  "(change (fsub x *0) x)",
  "(change (fsub x (fminus y)) (fadd x y))",

  // Multiplication
  "(change (mul x 0) 0)",
  "(change (mul x 1) x)",
  "(change (mul x -1) (minus x))",
  "(change (mul 'a 'b) (!mul 'a 'b))",
  "(change (mul 'a x) (mul x 'a))",
  "(change (mul (mul x 'a) 'b) (mul x (!mul 'a 'b)))",

  // FP multiplication
  "(change (fmul *a *b) (?mul *a *b))",
  "(change (fmul x *0) *0)",
  "(change (fmul x *1) x)",
  "(change (fmul x *2) (fadd x x))",
  "(change (fmul x *-1) (fminus x))",
  "(change (fmul *a x) (fmul x *a))",

  // Division
  "(change (div 0 x) 0)",
  "(change (div x 1) x)",
  "(change (div x -1) (minus x))",
  "(change (div x x) 1)",
  "(change (div 'a 'b) (!div 'a 'b))",
  "(change (div (mul x 'a) 'b) (!only-if (!eq (!mod 'a 'b) 0) (mul x (!div 'a 'b))))",
  "(change (div (div x 'a) 'b) (!only-if (!ne (!mul 'a 'b) -1) (div x (!mul 'a 'b))))",

  // FP Division
  "(change (fdiv *a *b) (?div *a *b))",
  "(change (fdiv *0 x) *0)",
  "(change (fdiv x *1) x)",
  "(change (fdiv x *-1) (fminus x))",
  "(change (fdiv x x) *1)",

  // Modulus
  "(change (mod x 1) 0)",
  "(change (mod x x) 0)",
  "(change (mod 0 x) 0)",
  "(change (mod 'a 'b) (!mod 'a 'b))",

  // Minus
  "(change (minus 'a) (!sub 0 'a))",
  "(change (minus (add x 'a)) (sub (!sub 0 'a) x))",
  "(change (minus (sub x y)) (sub y x))",
  "(change (minus (minus x)) x)",

  // FP Minus
  "(change (fminus *a) (?sub *0 *a))",
  "(change (fminus (fadd x *a)) (fsub (?sub *0 *a) x))",
  "(change (fminus (fsub x y)) (fsub y x))",
  "(change (fminus (fminus x)) x)",

  // Equality
  "(change (eq x x) 1)",
  "(change (eq 'a 'b) (!eq 'a 'b))",
  "(change (eq 'a x) (eq x 'a))",
  "(change (eq (add x 'a) 'b) (eq x (!sub 'b 'a)))",
  "(change (eq (sub x 'a) 'b) (eq x (!add 'b 'a)))",
  "(change (eq (mul x 'a) 'b) (!only-if (!eq 0 (!mod 'b 'a)) (eq x (!div 'b 'a))))",
  "(change (eq (mul x 'a) 'b) (!only-if (!ne 0 (!mod 'b 'a)) 0))",
  "(change (eq (div x 'a) 'b) (!only-if (!gt 'a 0) (and (lt x (!mul (!add 'b 1) 'a)) (ge x (!mul 'b 'a)))))",
  "(change (eq (mod x 'a) 'b) (!only-if (!le 'a 'b) 0))",
  "(change (eq (mod x 2) 1) (mod x 2))",
  "(change (eq x 0) (not x))",
  "(change (eq (not x) 0) (snz x))",

  // FP Equality
  "(change (feq x x) 1)",
  "(change (feq *a *b) (!feq *a *b))",
  "(change (feq *a x) (feq x *a))",

  // Less than or equal
  "(change (le x x) 1)",
  "(change (le 'a 'b) (!le 'a 'b))",
  "(change (le x 'a) (lt x (!add 'a 1)))",
  "(change (le 'a x) (lt (!sub 'a 1) x))",

  // FP Less than or equal
  "(change (fle x x) 1)",
  "(change (fle *a *b) (!fle *a *b))",

  // Less than
  "(change (lt x x) 0)",
  "(change (lt 'a 'b) (!lt 'a 'b))",
  "(change (lt (add x 'a) 'b) (lt x (!sub 'b 'a)))",
  "(change (lt (sub x 'a) 'b) (lt x (!add 'b 'a)))",
  "(change (lt (mul x 'a) 'b) (!only-if (!and (!gt 'a 0) (!gt 'b 0)) (lt x (!div 'b 'a))))",
  "(change (lt (div x 'a) 'b) (!only-if (!and (!gt 'a 0) (!gt 'b 0)) (lt x (!mul 'b 'a))))",
  "(change (lt 'b (add x 'a)) (lt (!sub 'b 'a) x))",
  "(change (lt 'b (sub x 'a)) (lt (!add 'b 'a) x))",
  "(change (lt 'b (mul x 'a)) (!only-if (!and (!gt 'a 0) (!gt 'b 0)) (lt (!div 'b 'a) x)))",
  "(change (lt 'b (div x 'a)) (!only-if (!and (!gt 'a 0) (!gt 'b 0)) (le (!mul 'a (!add 'b 1)) x)))",
  
  // FP Less than
  "(change (flt x x) 0)",
  "(change (flt *a *b) (!flt *a *b))",

  // Not
  "(change (not 'a) (!not 'a))",
  "(change (not (eq x y)) (ne x y))",
  "(change (not (lt x y)) (ge x y))",
  "(change (not (le x y)) (gt x y))",
  "(change (not (ne x y)) (eq x y))",
  "(change (not (not x)) (snz x))",
  "(change (not (or x y)) (and (not x) (not y)))",
  "(change (not (and x y)) (or (not x) (not y)))",

  // FP Not
  "(change (not *a) (!feq *a *0))",
  "(change (not (feq x y)) (fne x y))",
  "(change (not (flt x y)) (flt y x))",
  "(change (not (fle x y)) (fle y x))",
  "(change (not (fne x y)) (feq x y))",

  // Not equal
  "(change (ne 'a 'b) (!ne 'a 'b))",
  "(change (ne 'a x) (ne x 'a))",
  "(change (ne (add x 'a) 'b) (ne x (!sub 'b 'a)))",
  "(change (ne (sub x 'a) 'b) (ne x (!add 'b 'a)))",
  "(change (ne (mod x 2) 1) (not (mod x 2)))",
  "(change (ne x 0) (snz x))",
  "(change (ne (not x) 0) (not x))",

  // FP Not equal
  "(change (fne *a *b) (!fne *a *b))",

  // Set not zero
  "(change (snz 0) 0)",
  "(change (snz 'a) (!only-if (!ne 'a 0) 1))",
  "(change (snz (mod x 2)) (mod x 2))",
  "(change (snz (eq x y)) (eq x y))",
  "(change (snz (le x y)) (le x y))",
  "(change (snz (lt x y)) (lt x y))",
  "(change (snz (ne x y)) (ne x y))",

  // FP Set not zero
  "(change (snz *a) (!fne *a *0))",

  // float -> int
  "(change (f2i *a) (!cvt *a))",

  // int -> float
  "(change (i2f 'a) (?cvt 'a))",
};

std::map<std::string, int> RegularFold::stats() {
  return {
    { "folded-ops", foldedTotal }
  };
}

void removePhiOperand(Op *phi, BasicBlock *from) {
  auto ops = phi->getOperands();
  std::vector<Attr*> attrs;
  for (auto attr : phi->getAttrs())
    attrs.push_back(attr->clone());

  phi->removeAllOperands();
  // This deletes attributes if their refcnt goes to zero.
  // That's why we cloned above.
  phi->removeAllAttributes();

  for (size_t i = 0; i < ops.size(); i++) {
    auto pred = FROM(attrs[i]);
    if (from == pred)
      continue;

    // Only preserve the operands that aren't from dead blocks.
    phi->pushOperand(ops[i]);
    phi->add<FromAttr>(pred);
  }
}

void tidyPhi(BasicBlock *bb, BasicBlock *from) {
  auto phis = bb->getPhis();
  for (auto phi : phis)
    removePhiOperand(phi, from);
}

void RegularFold::run() {
  auto funcs = collectFuncs();
  int folded;
  do {
    folded = 0;
    for (auto func : funcs) {
      auto region = func->getRegion();

      for (auto bb : region->getBlocks()) {
        auto ops = bb->getOps();
        for (auto op : ops) {
          for (auto &rule : rules) {
            bool success = rule.rewrite(op);
            if (success) {
              folded++;
              break;
            }
          }
        }
      }
    }

    // Also, run some extra folds.
    Builder builder;

    runRewriter([&](BranchOp *op) {
      auto cond = op->DEF();

      // (br (snz x)) becomes (br x)
      // Do note that "set-not-zero" of float cannot be fold.
      if (isa<SetNotZeroOp>(cond) && cond->DEF()->getResultType() != Value::f32) {
        folded++;
        auto def = cond->DEF();
        builder.replace<BranchOp>(op, { def }, op->getAttrs());
        return true;
      }
      
      // (br (not x) >bb >bb2) becomes (br x >bb2 >bb)
      if (isa<NotOp>(cond) && cond->DEF()->getResultType() != Value::f32) {
        folded++;
        auto def = cond->DEF();
        builder.replace<BranchOp>(op, { def }, { new TargetAttr(ELSE(op)), new ElseAttr(TARGET(op)) });
        return true;
      }

      if (!isa<IntOp>(cond))
        return false;
      
      if (V(cond) == 0) {
        folded++;
        tidyPhi(TARGET(op), op->getParent());
        builder.replace<GotoOp>(op, { new TargetAttr(ELSE(op)) });
        return false;
      }

      // V(cond) != 0
      folded++;
      tidyPhi(ELSE(op), op->getParent());
      builder.replace<GotoOp>(op, { new TargetAttr(TARGET(op)) });
      return false;
    });

    foldedTotal += folded;
  } while (folded);
}
