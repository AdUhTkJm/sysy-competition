#include "ArmPasses.h"
#include "ArmMatcher.h"

using namespace sys;
using namespace sys::arm;

// First we only do lower, without any optimizations.
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

  "(change (not x) (cseteq (tst x x)))",

  "(change (br x >ifso >ifnot) (cbz x >ifso >ifnot))",

  "(change 'a (mov 'a))",
};

#define REPLACE(BeforeTy, AfterTy) \
  runRewriter([&](BeforeTy *op) { \
    builder.replace<AfterTy>(op, op->getOperands(), op->getAttrs()); \
    return true; \
  });

// Collect allocas, and take them as an offset from base pointer (though we don't really use base pointer).
// Also mark the function with an offset attribute.
static void rewriteAlloca(FuncOp *func) {
  Builder builder;

  auto region = func->getRegion();
  auto block = region->getFirstBlock();

  // All alloca's are in the first block.
  size_t offset = 0; // Offset from sp.
  size_t total = 0; // Total stack frame size
  std::vector<AllocaOp*> allocas;
  for (auto op : block->getOps()) {
    if (!isa<AllocaOp>(op))
      continue;

    size_t size = SIZE(op);
    total += size;
    allocas.push_back(cast<AllocaOp>(op));
  }

  for (auto op : allocas) {
    // Translate itself into `sp + offset`.
    builder.setBeforeOp(op);
    auto spValue = builder.create<ReadXRegOp>({ new RegAttr(Reg::x30) });
    auto offsetValue = builder.create<MovIOp>({ new IntAttr(offset) });
    auto add = builder.create<AddXOp>({ spValue, offsetValue });
    op->replaceAllUsesWith(add);

    size_t size = SIZE(op);
    offset += size;
    op->erase();
  }

  func->add<StackOffsetAttr>(total);
}


// We delay handling of calls etc. to RegAlloc.
void Lower::run() {
  Builder builder;

  REPLACE(ReturnOp, RetOp);
  REPLACE(GetGlobalOp, AdrOp);
  REPLACE(CallOp, BrOp);

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

  static const Reg fargRegs[] = {
    Reg::v0, Reg::v1, Reg::v2, Reg::v3,
    Reg::v4, Reg::v5, Reg::v6, Reg::v7,
  };
  static const Reg argRegs[] = {
    Reg::x0, Reg::x1, Reg::x2, Reg::x3,
    Reg::x4, Reg::x5, Reg::x6, Reg::x7,
  };

  runRewriter([&](sys::CallOp *op) {
    builder.setBeforeOp(op);
    const auto &args = op->getOperands();

    std::vector<Value> argsNew;
    std::vector<Value> fargsNew;
    std::vector<Value> spilled;
    for (size_t i = 0; i < args.size(); i++) {
      Value arg = args[i];
      auto ty = arg.defining->getResultType();
      int fcnt = fargsNew.size();
      int cnt = argsNew.size();

      if (ty == Value::f32 && fcnt < 8) {
        fargsNew.push_back(builder.create<WriteRegOp>({ arg }, { new RegAttr(fargRegs[fcnt]) }));
        continue;
      }
      if (ty != Value::f32 && cnt < 8) {
        argsNew.push_back(builder.create<WriteRegOp>({ arg }, { new RegAttr(argRegs[cnt]) }));
        continue;
      }
      spilled.push_back(arg);
    }

    // More registers must get spilled to stack.
    int stackOffset = spilled.size() * 8;
    // Align to 16 bytes.
    if (stackOffset % 16 != 0)
      stackOffset = stackOffset / 16 * 16 + 16;
    if (stackOffset > 0)
      builder.create<SubSpOp>({ new IntAttr(stackOffset) });
    
    for (int i = 0; i < spilled.size(); i++) {
      auto sp = builder.create<ReadXRegOp>({ new RegAttr(Reg::x30) });
      builder.create<StoreOp>({ spilled[i], sp }, { new SizeAttr(8), new IntAttr(i * 8) });
    }

    builder.create<BrOp>(argsNew, { 
      op->get<NameAttr>(),
      new ArgCountAttr(args.size())
    });

    // Restore stack pointer.
    if (stackOffset > 0)
      builder.create<SubSpOp>({ new IntAttr(-stackOffset) });

    // Read result from a0.
    if (op->getResultType() == Value::f32)
      builder.replace<ReadFRegOp>(op, { new RegAttr(Reg::v0) });
    else
      builder.replace<ReadRegOp>(op, { new RegAttr(Reg::x0) });
    return true;
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

    rewriteAlloca(func);
  }
}
