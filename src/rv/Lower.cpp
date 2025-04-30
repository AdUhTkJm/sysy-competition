#include "RvPasses.h"
#include "RvOps.h"
#include "RvAttrs.h"
#include "../codegen/CodeGen.h"
#include "../codegen/Attrs.h"

using namespace sys::rv;
using namespace sys;

// Combines all alloca's into a SubSpOp.
// Also rewrites load/stores with sp-offset.
void rewriteAlloca(FuncOp *func) {
  Builder builder;

  auto region = func->getRegion();
  auto block = region->getFirstBlock();

  // If the first Op isn't alloca, then the whole function doesn't contain alloca.
  // This is guaranteed by MoveAlloca pass.
  if (!isa<AllocaOp>(block->getFirstOp()))
    return;

  // All alloca's are in the first block.
  size_t offset = 0; // Offset from sp.
  size_t total = 0; // Total stack frame size
  std::vector<AllocaOp*> allocas;
  for (auto op : block->getOps()) {
    if (!isa<AllocaOp>(op))
      continue;

    size_t size = op->getAttr<SizeAttr>()->value;
    total += size;
    allocas.push_back(cast<AllocaOp>(op));
  }

  for (auto op : allocas) {
    // Translate itself into `sp + offset`.
    builder.setBeforeOp(op);
    auto spValue = builder.create<ReadRegOp>({
      new RegAttr(Reg::sp)
    });

    auto offsetValue = builder.create<LiOp>({
      new IntAttr(offset)
    });
    auto add = builder.create<AddOp>({ spValue, offsetValue });
    op->replaceAllUsesWith(add);

    size_t size = op->getAttr<SizeAttr>()->value;
    offset += size;
    op->erase();
  }

  // Allocate space equal to all allocas.
  builder.setToBlockStart(block);
  builder.create<SubSpOp>({
    new IntAttr(total)
  });
}

#define REPLACE(BeforeTy, AfterTy) \
  runRewriter([&](BeforeTy *op) { \
    builder.replace<AfterTy>(op, op->getOperands(), op->getAttrs()); \
    return true; \
  });


void Lower::run() {
  Builder builder;

  REPLACE(IntOp, LiOp);
  REPLACE(AddIOp, AddwOp);
  REPLACE(AddLOp, AddOp);
  REPLACE(SubIOp, SubwOp);
  REPLACE(SubLOp, SubOp);
  REPLACE(MulIOp, MulwOp);
  REPLACE(MulLOp, MulOp);
  REPLACE(MulshOp, MulhOp);
  REPLACE(MuluhOp, MulhuOp);
  REPLACE(DivIOp, DivwOp);
  REPLACE(DivLOp, DivOp);
  REPLACE(LShiftImmOp, SlliwOp);
  REPLACE(RShiftImmOp, SraiwOp);
  REPLACE(RShiftImmLOp, SraiOp);
  REPLACE(GotoOp, JOp);
  REPLACE(GetGlobalOp, LaOp);
  REPLACE(NotOp, SeqzOp);
  REPLACE(SetNotZeroOp, SnezOp);
  REPLACE(AndIOp, AndOp);
  REPLACE(OrIOp, OrOp);
  REPLACE(XorIOp, XorOp);

  runRewriter([&](MinusOp *op) {
    auto value = op->getOperand();
    
    builder.setBeforeOp(op);
    auto zero = builder.create<LiOp>({ new IntAttr(0) });
    builder.replace<SubOp>(op, { zero, value }, op->getAttrs());
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
    auto zero = builder.create<ReadRegOp>({ new RegAttr(Reg::zero) });
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
    auto load = builder.replace<sys::rv::LoadOp>(op, op->getOperands(), op->getAttrs());
    load->addAttr<IntAttr>(0);
    return true;
  });

  runRewriter([&](sys::StoreOp *op) {
    auto store = builder.replace<sys::rv::StoreOp>(op, op->getOperands(), op->getAttrs());
    store->addAttr<IntAttr>(0);
    return true;
  });

  runRewriter([&](ReturnOp *op) {
    builder.setBeforeOp(op);

    if (op->getOperands().size()) {
      auto virt = builder.create<WriteRegOp>(op->getOperands(), {
        new RegAttr(Reg::a0)
      });
      builder.replace<RetOp>(op, { virt });
      return true;
    }
    
    builder.replace<RetOp>(op);
    return true;
  });

  static Reg regs[] = {
    Reg::a0, Reg::a1, Reg::a2, Reg::a3,
    Reg::a4, Reg::a5, Reg::a6, Reg::a7,
  };

  runRewriter([&](sys::CallOp *op) {
    builder.setBeforeOp(op);
    const auto &args = op->getOperands();
    // TODO: spilling
    assert(args.size() <= 8);

    std::vector<Value> argsNew;
    for (int i = 0; i < args.size(); i++)
      argsNew.push_back(
      builder.create<WriteRegOp>({ args[i] },
       {
            new RegAttr(regs[i])
          })
      );

    builder.create<sys::rv::CallOp>(argsNew, { 
      op->getAttr<NameAttr>(),
      new ArgCountAttr(args.size())
    });

    // Read result from a0.
    builder.replace<ReadRegOp>(op, { new RegAttr(Reg::a0) });
    return true;
  });

  auto funcs = module->findAll<FuncOp>();
  for (auto func : funcs)
    rewriteAlloca(func);
}
