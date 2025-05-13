#include "ArmPasses.h"

using namespace sys;
using namespace sys::arm;

bool Destruct::spilled(Op *op) {
  return spillOffset.count(op);
}

void lowerBinary(Op *op) {
  
}

void createSpill(Op *op, int offset) {
  Builder builder;
  builder.setAfterOp(op);
  
  auto ty = op->getResultType();
  switch (ty) {
  case Value::i32:
    builder.create<SpillOp>({ new IntAttr(offset) });
    break;
  case Value::i64:
    builder.create<SpillLOp>({ new IntAttr(offset) });
    break;
  case Value::f32:
    builder.create<SpillFOp>({ new IntAttr(offset) });
    break;
  default:
    assert(false);
  }
}

void Destruct::runImpl(Region *region) {
  Builder builder;

  // Insert spill and reload.
  for (auto bb : region->getBlocks()) {
    auto ops = bb->getOps();

    for (auto op : ops) {
      if (spilled(op))
        createSpill(op, spillOffset[op]);
    }
  }

  // For reloads, they must be 
}

void Destruct::run() {
  RegAlloc allocator(module);
  allocator.run();
  assignment = std::move(allocator.assignment);
  spillOffset = std::move(allocator.spillOffset);

  auto funcs = collectFuncs();

  for (auto func : funcs)
    runImpl(func->getRegion());
}
