#include "RvPasses.h"
#include "RvOps.h"
#include "RvAttrs.h"
#include "../codegen/CodeGen.h"
#include "../codegen/Attrs.h"

using namespace sys::rv;
using namespace sys;

// Combines all alloca's into a SubSpOp.
// Also rewrites load/stores with sp-offset.
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
    auto spValue = builder.create<ReadRegOp>(Value::i32, { new RegAttr(Reg::sp) });
    auto offsetValue = builder.create<LiOp>({ new IntAttr(offset) });
    auto add = builder.create<AddOp>({ spValue, offsetValue });
    op->replaceAllUsesWith(add);

    size_t size = SIZE(op);
    offset += size;
    op->erase();
  }

  func->add<StackOffsetAttr>(total);
}

#define REPLACE(BeforeTy, AfterTy) \
  runRewriter([&](BeforeTy *op) { \
    builder.replace<AfterTy>(op, op->getOperands(), op->getAttrs()); \
    return true; \
  });


void Lower::run() {
  Builder builder;

  // First fix type of phi's.
  // If a phi has an operand of float type, then itself must also be of float type.
  runRewriter([&](PhiOp *op) {
    if (op->getResultType() == Value::f32)
      return false;

    for (auto operand : op->getOperands()) {
      if (operand.defining->getResultType() == Value::f32) {
        op->setResultType(Value::f32);
        return true;
      }
    }

    return false;
  });

  // When `x` is 0 or 1, we have:
  //   x ? y : z = (-x & y) | ((x - 1) & z)
  //
  // I strongly doubt if the competition has a good branch predictor.
  runRewriter([&](SelectOp *op) {
    auto x = op->DEF(0), y = op->DEF(1), z = op->DEF(2);
    builder.setBeforeOp(op);
    auto snez = builder.create<SnezOp>({ x });
    auto zero = builder.create<LiOp>({ new IntAttr(0) });
    auto neg = builder.create<SubOp>({ zero, snez });
    auto sub = builder.create<AddiOp>({ snez }, { new IntAttr(-1) });
    Value andy = builder.create<AndOp>({ neg, y });
    Value andz = builder.create<AndOp>({ sub, z });
    builder.replace<OrOp>(op, { andy, andz });
    return false;
  });

  REPLACE(IntOp, LiOp);
  REPLACE(AddIOp, AddwOp);
  REPLACE(AddLOp, AddOp);
  REPLACE(SubIOp, SubwOp);
  REPLACE(MulIOp, MulwOp);
  REPLACE(MulLOp, MulOp);
  REPLACE(MulshOp, MulhOp);
  REPLACE(MuluhOp, MulhuOp);
  REPLACE(DivIOp, DivwOp);
  REPLACE(ModIOp, RemwOp);
  REPLACE(ModLOp, RemOp);
  REPLACE(LShiftOp, SllwOp);
  REPLACE(LShiftLOp, SllOp);
  REPLACE(RShiftOp, SrawOp);
  REPLACE(RShiftLOp, SraOp);
  REPLACE(GotoOp, JOp);
  REPLACE(GetGlobalOp, LaOp);
  REPLACE(AndIOp, AndOp);
  REPLACE(OrIOp, OrOp);
  REPLACE(XorIOp, XorOp);
  REPLACE(AddFOp, FaddOp);
  REPLACE(SubFOp, FsubOp);
  REPLACE(MulFOp, FmulOp);
  REPLACE(DivFOp, FdivOp);
  REPLACE(LtFOp, FltOp);
  REPLACE(EqFOp, FeqOp);
  REPLACE(LeFOp, FleOp);
  REPLACE(F2IOp, FcvtwsRtzOp);
  REPLACE(I2FOp, FcvtswOp);
  
  runRewriter([&](FloatOp *op) {
    float value = F(op);
    
    builder.setBeforeOp(op);
    // Strict aliasing? Don't know.
    auto li = builder.create<LiOp>({ new IntAttr(*(int*) &value) });
    builder.replace<FmvwxOp>(op, { li });
    return true;
  });

  runRewriter([&](MinusOp *op) {
    auto value = op->getOperand();
    
    builder.setBeforeOp(op);
    auto zero = builder.create<LiOp>({ new IntAttr(0) });
    builder.replace<SubOp>(op, { zero, value }, op->getAttrs());
    return true;
  });

  runRewriter([&](MinusFOp *op) {
    auto value = op->getOperand();
    
    builder.setBeforeOp(op);
    auto zero = builder.create<LiOp>({ new IntAttr(0) });
    auto zerof = builder.create<FmvwxOp>({ zero });
    builder.replace<FsubOp>(op, { zerof, value }, op->getAttrs());
    return true;
  });

  runRewriter([&](ModIOp *op) {
    auto denom = op->getOperand(0);
    auto nom = op->getOperand(1);

    builder.setBeforeOp(op);
    auto quot = builder.create<DivwOp>(op->getOperands(), op->getAttrs());
    auto mul = builder.create<MulwOp>({ quot, nom });
    builder.replace<SubOp>(op, { denom, mul });
    return true;
  });
  
  runRewriter([&](ModFOp *op) {
    auto denom = op->getOperand(0);
    auto nom = op->getOperand(1);

    builder.setBeforeOp(op);
    auto quot = builder.create<FdivOp>(op->getOperands(), op->getAttrs());
    auto mul = builder.create<FmulOp>({ quot, nom });
    builder.replace<FsubOp>(op, { denom, mul });
    return true;
  });

  runRewriter([&](SetNotZeroOp *op) {
    if (op->DEF()->getResultType() == Value::f32) {
      builder.setBeforeOp(op);
      auto zero = builder.create<LiOp>({ new IntAttr(0) });
      auto zerof = builder.create<FmvwxOp>({ zero });
      auto iszero = builder.create<FeqOp>({ op->getOperand(), zerof });
      builder.replace<SeqzOp>(op, { iszero });
      return true;
    }

    builder.replace<SnezOp>(op, op->getOperands(), op->getAttrs());
    return true;
  });

  runRewriter([&](NotOp *op) {
    if (op->DEF()->getResultType() == Value::f32) {
      builder.setBeforeOp(op);
      auto zero = builder.create<LiOp>({ new IntAttr(0) });
      auto zerof = builder.create<FmvwxOp>({ zero });
      builder.replace<FeqOp>(op, { op->getOperand(), zerof });
      return true;
    }

    builder.replace<SeqzOp>(op, op->getOperands(), op->getAttrs());
    return true;
  });

  runRewriter([&](BranchOp *op) {
    auto cond = op->getOperand().defining;

    if (isa<EqOp>(cond)) {
      builder.replace<BeqOp>(op, cond->getOperands(), op->getAttrs());
      return true;
    }

    if (isa<NeOp>(cond)) {
      builder.replace<BneOp>(op, cond->getOperands(), op->getAttrs());
      return true;
    }

    // Note RISC-V only has `bge`. Switch operand order for this.
    if (isa<LeOp>(cond)) {
      auto v1 = cond->getOperand(0);
      auto v2 = cond->getOperand(1);
      builder.replace<BgeOp>(op, { v2, v1 }, op->getAttrs());
      return true;
    }

    if (isa<LtOp>(cond)) {
      builder.replace<BltOp>(op, cond->getOperands(), op->getAttrs());
      return true;
    }

    builder.setBeforeOp(op);
    auto zero = builder.create<ReadRegOp>(Value::i32, { new RegAttr(Reg::zero) });
    builder.replace<BneOp>(op, { cond, zero }, op->getAttrs());
    return true;
  });

  // Delay these after selection of BranchOp.
  REPLACE(LtOp, SltOp);

  runRewriter([&](EqOp *op) {
    builder.setBeforeOp(op);
    // 'xor' is a keyword of C++.
    auto xorOp = builder.create<XorOp>(op->getOperands(), op->getAttrs());
    builder.replace<SeqzOp>(op,{ xorOp });
    return true;
  });

  runRewriter([&](NeOp *op) {
    builder.setBeforeOp(op);
    // 'xor' is a keyword of C++.
    auto xorOp = builder.create<XorOp>(op->getOperands(), op->getAttrs());
    builder.replace<SnezOp>(op,{ xorOp });
    return true;
  });

  runRewriter([&](NeFOp *op) {
    builder.setBeforeOp(op);
    auto feq = builder.create<FeqOp>(op->getOperands(), op->getAttrs());
    builder.replace<SeqzOp>(op, { feq });
    return true;
  });

  runRewriter([&](LeOp *op) {
    builder.setBeforeOp(op);
    auto l = op->getOperand(0);
    auto r = op->getOperand(1);
    // Turn (l <= r) into !(r < l).
    auto xorOp = builder.create<SltOp>({ r, l }, op->getAttrs());
    builder.replace<SeqzOp>(op,{ xorOp });
    return true;
  });

  runRewriter([&](sys::LoadOp *op) {
    auto load = builder.replace<sys::rv::LoadOp>(op, op->getResultType(), op->getOperands(), op->getAttrs());
    load->add<IntAttr>(0);
    return true;
  });

  runRewriter([&](sys::StoreOp *op) {
    auto store = builder.replace<sys::rv::StoreOp>(op, op->getOperands(), op->getAttrs());
    store->add<IntAttr>(0);
    return true;
  });

  runRewriter([&](ReturnOp *op) {
    builder.setBeforeOp(op);

    if (op->getOperands().size()) {
      auto fp = op->DEF(0)->getResultType() == Value::f32;
      auto virt = builder.create<WriteRegOp>(op->getOperands(), {
        new RegAttr(fp ? Reg::fa0 : Reg::a0)
      });
      builder.replace<RetOp>(op, { virt });
      return true;
    }
    
    builder.replace<RetOp>(op);
    return true;
  });

  const static Reg regs[] = {
    Reg::a0, Reg::a1, Reg::a2, Reg::a3,
    Reg::a4, Reg::a5, Reg::a6, Reg::a7,
  };
  const static Reg fregs[] = {
    Reg::fa0, Reg::fa1, Reg::fa2, Reg::fa3,
    Reg::fa4, Reg::fa5, Reg::fa6, Reg::fa7,
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
        fargsNew.push_back(builder.create<WriteRegOp>({ arg }, { new RegAttr(fregs[fcnt]) }));
        continue;
      }
      if (ty != Value::f32 && cnt < 8) {
        argsNew.push_back(builder.create<WriteRegOp>({ arg }, { new RegAttr(regs[cnt]) }));
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
      auto sp = builder.create<ReadRegOp>(Value::i32, { new RegAttr(Reg::sp) });
      builder.create<StoreOp>({ spilled[i], sp }, { new SizeAttr(8), new IntAttr(i * 8) });
    }

    builder.create<sys::rv::CallOp>(argsNew, { 
      op->get<NameAttr>(),
      new ArgCountAttr(args.size())
    });

    // Restore stack pointer.
    if (stackOffset > 0)
      builder.create<SubSpOp>({ new IntAttr(-stackOffset) });

    // Read result from a0.
    if (op->getResultType() == Value::f32)
      builder.replace<ReadRegOp>(op, Value::f32, { new RegAttr(Reg::fa0) });
    else
      builder.replace<ReadRegOp>(op, Value::i32, { new RegAttr(Reg::a0) });
    return true;
  });

  auto funcs = collectFuncs();
  for (auto func : funcs)
    rewriteAlloca(func);
}
