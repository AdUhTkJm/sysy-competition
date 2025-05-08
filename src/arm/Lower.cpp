#include "ArmPasses.h"
#include "ArmMatcher.h"

using namespace sys;
using namespace sys::arm;

// First we only do lower, rather than anything else.
// More folds are in InstCombine.
const ArmRule rules[] = {
  "(change (add x y) (addw x y))",
  "(change (addl x y) (addx x y))",

  "(change (sub x y) (subw x y))",

  "(change (mul x y) (mulw x y))",
  "(change (mull x y) (mulx x y))",

  "(change (div x y) (sdivw x y))",
  
  "(change (and x y) (and x y))",

  "(change (shl x 'a) (lsli x 'a))",
  "(change (shr x 'a) (asrwi x 'a))",
  "(change (shrl x 'a) (asrxi x 'a))",

  "(change (j >bb) (b >bb))",

  "(change (ne x y) (csetne (cmp x y)))",
  "(change (lt x y) (csetlt (cmp x y)))",
  "(change (le x y) (csetle (cmp x y)))",
  "(change (eq x y) (cseteq (cmp x y)))",

  "(change (br x >bb ?bb2) (cbz x >bb ?bb2))",

  "(change 'a (mov 'a))",
};

// First of all, make all ops with an immediate accept an IntOp instead.
// That's because we can't handle immediates at IR level in this Lisp-like language.
#define DESTRUCT_IMM(Ty) \
  runRewriter([&](Ty *op) { \
    builder.setBeforeOp(op); \
    auto imm = builder.create<IntOp>({ new IntAttr(V(op)) }); \
    op->removeAllAttributes(); \
    op->pushOperand(imm); \
    return false; \
  });

#define REPLACE(BeforeTy, AfterTy) \
  runRewriter([&](BeforeTy *op) { \
    builder.replace<AfterTy>(op, op->getOperands(), op->getAttrs()); \
    return true; \
  });

// We delay handling of calls etc. to RegAlloc.
void Lower::run() {
  Builder builder;

  DESTRUCT_IMM(LShiftImmOp);
  DESTRUCT_IMM(RShiftImmOp);
  DESTRUCT_IMM(RShiftImmLOp);
  REPLACE(ReturnOp, RetOp);

  auto funcs = collectFuncs();
  // No need to iterate to fixed point. All Ops are guaranteed to transform.
  for (auto func : funcs) {
    auto region = func->getRegion();

    for (auto bb : region->getBlocks()) {
      auto ops = bb->getOps();

      for (auto op : ops) {
        for (auto rule : rules) {
          bool success = rule.rewriteForLower(op);
          if (success)
            break;
        }
      }
    }
  }
}
