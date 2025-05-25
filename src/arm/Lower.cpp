#include "ArmPasses.h"

using namespace sys;
using namespace sys::arm;

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
    auto spValue = builder.create<ReadRegOp>({ new RegAttr(Reg::sp) });
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

  REPLACE(GetGlobalOp, AdrOp);
  REPLACE(AddIOp, AddWOp);
  REPLACE(AddLOp, AddXOp);
  REPLACE(SubIOp, SubWOp);
  REPLACE(MulIOp, MulWOp);
  REPLACE(MulLOp, MulXOp);
  REPLACE(DivIOp, SdivWOp);
  REPLACE(LShiftLOp, LslXOp);
  REPLACE(LShiftOp, LslWOp);
  REPLACE(RShiftLOp, AsrXIOp);
  REPLACE(RShiftOp, AsrWIOp);
  REPLACE(AndIOp, AndOp);
  REPLACE(OrIOp, OrOp);
  REPLACE(XorIOp, EorOp);
  REPLACE(GotoOp, BOp);
  REPLACE(BranchOp, CbnzOp);
  REPLACE(IntOp, MovIOp);
  REPLACE(F2IOp, FcvtzsOp);
  REPLACE(I2FOp, ScvtfOp);
  REPLACE(AddFOp, FaddOp);
  REPLACE(SubFOp, FsubOp);
  REPLACE(MulFOp, FmulOp);
  REPLACE(DivFOp, FdivOp);
  REPLACE(MinusOp, NegOp);
  REPLACE(MinusFOp, FnegOp);

  runRewriter([&](FloatOp *op) {
    float value = F(op);
    
    builder.setBeforeOp(op);
    // Strict aliasing? Don't know.
    auto li = builder.create<MovIOp>({ new IntAttr(*(int*) &value) });
    builder.replace<FmovWOp>(op, { li });
    return true;
  });

  runRewriter([&](NotOp *op) {
    Value def = op->getOperand();

    builder.setBeforeOp(op);
    Value tst = builder.create<TstOp>({ def, def });
    builder.replace<CsetEqOp>(op, { tst });
    return false;
  });

  runRewriter([&](SetNotZeroOp *op) {
    Value def = op->getOperand();

    builder.setBeforeOp(op);
    Value tst = builder.create<TstOp>({ def, def });
    builder.replace<CsetNeOp>(op, { tst });
    return false;
  });

  runRewriter([&](EqOp *op) {
    Value x = op->getOperand(0);
    Value y = op->getOperand(1);

    builder.setBeforeOp(op);
    Value tst = builder.create<CmpOp>({ x, y });
    builder.replace<CsetEqOp>(op, { tst });
    return false;
  });

  runRewriter([&](NeOp *op) {
    Value x = op->getOperand(0);
    Value y = op->getOperand(1);

    builder.setBeforeOp(op);
    Value tst = builder.create<CmpOp>({ x, y });
    builder.replace<CsetNeOp>(op, { tst });
    return false;
  });

  runRewriter([&](LtOp *op) {
    Value x = op->getOperand(0);
    Value y = op->getOperand(1);

    builder.setBeforeOp(op);
    Value tst = builder.create<CmpOp>({ x, y });
    builder.replace<CsetLtOp>(op, { tst });
    return false;
  });

  runRewriter([&](LeOp *op) {
    Value x = op->getOperand(0);
    Value y = op->getOperand(1);

    builder.setBeforeOp(op);
    Value tst = builder.create<CmpOp>({ x, y });
    builder.replace<CsetLeOp>(op, { tst });
    return false;
  });

  runRewriter([&](ModIOp *op) {
    auto x = op->getOperand(0);
    auto y = op->getOperand(1);

    builder.setBeforeOp(op);
    auto sdiv = builder.create<SdivWOp>({ x, y });
    builder.replace<MsubWOp>(op, { sdiv, y, x });
    return false;
  });

  runRewriter([&](StoreOp *op) {
    if (op->DEF(0)->getResultType() == Value::f32) {
      builder.replace<StrFOp>(op, op->getOperands(), { new IntAttr(0) });
      return false;
    }

    if (SIZE(op) == 8) {
      builder.replace<StrXOp>(op, op->getOperands(), { new IntAttr(0) });
      return false;
    }

    builder.replace<StrWOp>(op, op->getOperands(), { new IntAttr(0) });
    return false;
  });

  runRewriter([&](LoadOp *op) {
    if (op->getResultType() == Value::f32) {
      builder.replace<LdrFOp>(op, op->getOperands(), { new IntAttr(0) });
      return false;
    }

    if (SIZE(op) == 8) {
      builder.replace<LdrXOp>(op, op->getOperands(), { new IntAttr(0) });
      return false;
    }

    builder.replace<LdrWOp>(op, op->getOperands(), { new IntAttr(0) });
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
      auto sp = builder.create<ReadRegOp>({ new RegAttr(Reg::sp) });
      builder.create<StoreOp>({ spilled[i], sp }, { new SizeAttr(8), new IntAttr(i * 8) });
    }

    builder.create<BlOp>(argsNew, { 
      op->get<NameAttr>(),
      new ArgCountAttr(args.size())
    });

    // Restore stack pointer.
    if (stackOffset > 0)
      builder.create<SubSpOp>({ new IntAttr(-stackOffset) });

    // Read result from a0.
    if (op->getResultType() == Value::f32)
      builder.replace<ReadRegOp>(op, { new RegAttr(Reg::v0) });
    else
      builder.replace<ReadRegOp>(op, { new RegAttr(Reg::x0) });
    return true;
  });

  runRewriter([&](ReturnOp *op) {
    builder.setBeforeOp(op);

    if (op->getOperands().size()) {
      auto fp = op->DEF(0)->getResultType() == Value::f32;
      auto virt = builder.create<WriteRegOp>(op->getOperands(), {
        new RegAttr(fp ? Reg::v0 : Reg::x0)
      });
      builder.replace<RetOp>(op, { virt });
      return true;
    }
    
    builder.replace<RetOp>(op);
    return true;
  });

  // Finally, convert all `mov x, 0` to reading from xzr.
  runRewriter([&](MovIOp *op) {
    if (V(op) == 0)
      builder.replace<ReadRegOp>(op, { new RegAttr(Reg::xzr) });
    
    return false;
  });

  auto funcs = collectFuncs();
  for (auto func : funcs) 
    rewriteAlloca(func);
}
