#include "Passes.h"
#include "../utils/Matcher.h"

using namespace sys;

#define INT(op) isa<IntOp>(op)

const Rule rules[] = {
  // Addition
  "(change (add x 0) x)",
  "(change (add 'a 'b) (!add 'a 'b))",
  "(change (add 'a x) (add x 'a))",
  "(change (add x (minus y)) (sub x y))",
  "(change (add (minus x) y) (sub y x))",
  "(change (add (add x 'a) 'b) (add x (!add 'a 'b)))",
  "(change (add (mul x 'a) (mul x 'b)) (mul x (!add 'a 'b)))",
  "(change (add (mul x 'a) (mul y 'a)) (mul (add x y) 'a))",
  "(change (add (div 'a x) (div 'b x)) (div (!add 'a 'b) x))",
  "(change (add (div x 'a) (div y 'a)) (div (add x y) 'a))",

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
  "(change (sub (mul x 'a) (mul y 'a)) (mul (sub x y) 'a))",
  "(change (sub (div 'a x) (div 'b x)) (div (!sub 'a 'b) x))",
  "(change (sub (div x 'a) (div y 'a)) (div (sub x y) 'a))",

  // Multiplication
  "(change (mul x 0) 0)",
  "(change (mul x 1) x)",
  "(change (mul x -1) (minus x))",
  "(change (mul 'a 'b) (!mul 'a 'b))",
  "(change (mul 'a x) (mul x 'a))",

  // Division
  "(change (div 0 x) 0)",
  "(change (div x 1) x)",
  "(change (div x -1) (minus x))",
  "(change (div x x) 1)",
  "(change (div 'a 'b) (!div 'a 'b))",

  // Modulus
  "(change (mod x 1) 0)",
  "(change (mod x x) 0)",
  "(change (mod 0 x) 0)",
  "(change (mod 'a 'b) (!mod 'a 'b))",

  // Equality
  "(change (eq x x) 1)",
  "(change (eq 'a 'b) (!eq 'a 'b))",
  "(change (eq 'a x) (eq x 'a))",
  "(change (eq (add x 'a) 'b) (eq x (!sub 'b 'a)))",
  "(change (eq (sub x 'a) 'b) (eq x (!add 'b 'a)))",
  "(change (eq (mul x 'a) 'b) (!only-if (!eq 0 (!mod 'b 'a)) (eq x (!div 'b 'a))))",
  "(change (eq (mul x 'a) 'b) (!only-if (!ne 0 (!mod 'b 'a)) 0))",
  "(change (eq (div x 'a) 'b) (!only-if (!gt 'a 0) (and (lt x (!mul (!add 'b 1) 'a)) (gt x (!mul 'b 'a)))))",
  "(change (eq (mod x 'a) 'b) (!only-if (!le 'a 'b) 0))",

  // Less than or equal
  "(change (le x x) 1)",
  "(change (le 'a 'b) (!le 'a 'b))",
  "(change (le x 'a) (lt x (!add 'a 1)))",
  "(change (le 'a x) (lt (!sub 'a 1) x))",

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
};

std::map<std::string, int> RegularFold::stats() {
  return {
    { "folded-ops", foldedTotal }
  };
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
          for (auto rule : rules)
            folded += rule.rewrite(op);
        }
      }
    }
    foldedTotal += folded;
  } while (folded);
}
