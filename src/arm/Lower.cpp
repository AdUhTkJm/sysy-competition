#include "ArmPasses.h"
#include "ArmMatcher.h"

using namespace sys;
using namespace sys::arm;

// First we only do lower, rather than anything else.
// More folds are in InstCombine.
static ArmRule rules[] = {
  "(change (add x y) (addw x y))",
  "(change (addl x y) (addx x y))",

  "(change (sub x y) (subw x y))",

  "(change (mul x y) (mulw x y))",
  "(change (mull x y) (mulx x y))",

  "(change (div x y) (sdivw x y))",

  "(change (shl x #a) (lsli x #a))",
  "(change (shr x #a) (asrwi x #a))",
  "(change (shrl x #a) (asrxi x #a))",
  
  "(change (and x y) (and x y))",

  "(change (j >bb) (b >bb))",

  "(change (ne x y) (csetne (cmp x y)))",
  "(change (lt x y) (csetlt (cmp x y)))",
  "(change (le x y) (csetle (cmp x y)))",
  "(change (eq x y) (cseteq (cmp x y)))",

  "(change (br x >bb ?bb2) (cbz x >bb ?bb2))",

  "(change 'a (mov 'a))",
};

#define REPLACE(BeforeTy, AfterTy) \
  runRewriter([&](BeforeTy *op) { \
    builder.replace<AfterTy>(op, op->getOperands(), op->getAttrs()); \
    return true; \
  });

// We delay handling of calls etc. to RegAlloc.
void Lower::run() {
  Builder builder;

  REPLACE(ReturnOp, RetOp);
  REPLACE(GetGlobalOp, AdrOp);

  runRewriter([&](StoreOp *op) {
    if (op->getResultType() == Value::f32) {
      builder.replace<StrFOp>(op, op->getOperands());
      return false;
    }

    if (SIZE(op) == 8) {
      builder.replace<StrXOp>(op, op->getOperands());
      return false;
    }

    builder.replace<StrWOp>(op, op->getOperands());
    return false;
  });

  runRewriter([&](LoadOp *op) {
    if (op->getResultType() == Value::f32) {
      builder.replace<LdrFOp>(op, op->getOperands());
      return false;
    }

    if (SIZE(op) == 8) {
      builder.replace<LdrXOp>(op, op->getOperands());
      return false;
    }

    builder.replace<LdrWOp>(op, op->getOperands());
    return false;
  });

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
