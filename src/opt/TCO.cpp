#include "PrePasses.h"

using namespace sys;

std::map<std::string, int> TCO::stats() {
  return {
    { "removed-calls", uncalled },
  };
}

void TCO::runImpl(FuncOp *func) {
  const auto &name = NAME(func);
  
  // Try to identify whether `func` is tail recursive.
  auto rets = func->findAll<ReturnOp>();
  // Whether a return uses the result of `call`.
  // If not, then there's no need to perform TCO even though it's correct to do.
  bool hasCallRet = false;

  const auto lastDone = [](Op *ret) -> Op* {
    if (ret->getOperandCount() == 0) {
      if (ret == ret->getParent()->getFirstOp())
        return nullptr;

      return ret->prevOp();
    }
    return ret->DEF();
  };

  for (auto ret : rets) {
    Op *def = lastDone(ret);
    if (!def)
      continue;

    if (isa<IntOp>(def) || isa<LoadOp>(def) || isa<FloatOp>(def))
      continue;

    if (isa<CallOp>(def) && NAME(def) == name && def->getUses().size() == 1) {
      hasCallRet = true;
      continue;
    }

    // Not tail recursive. Return.
    return;
  }

  if (!hasCallRet)
    return;

  uncalled++;
  auto region = func->getRegion();
  
  Builder builder;

  // Put the whole function body inside a WhileOp.
  auto tail = region->appendBlock();
  builder.setToBlockStart(tail);

  auto loop = builder.create<WhileOp>();
  auto before = loop->appendRegion();
  auto after = loop->appendRegion();
  auto afterEntry = after->appendBlock();

  auto bbs = region->getBlocks();
  for (auto bb : bbs) {
    if (bb != tail)
      bb->moveAfter(afterEntry);
  }

  int argcnt = func->get<ArgCountAttr>()->count;
  std::vector<Op*> allocas;
  allocas.resize(argcnt);

  auto getargs = func->findAll<GetArgOp>();

  auto newEntry = region->appendBlock();
  newEntry->moveBefore(tail);

  for (auto getarg : getargs) {
    // Before mem2reg, the only use for a getarg is to be stored in an alloca.
    auto store = *getarg->getUses().begin();
    allocas[V(getarg)] = store->DEF(1);

    // Also, pull them out of loop.
    getarg->moveToEnd(newEntry);
    store->moveToEnd(newEntry);
  }

  // Pull allocas out of loop.
  auto allAllocas = func->findAll<AllocaOp>();
  for (auto alloca : allAllocas)
    alloca->moveToStart(newEntry);

  builder.setToBlockStart(newEntry);
  for (auto ret : rets) {
    auto def = lastDone(ret);

    // Store the arguments into correct allocas.
    // Replace this call with a `continue`.
    if (isa<CallOp>(def)) {
      for (int i = 0; i < def->getOperandCount(); i++) {
        Value op = def->getOperand(i);
        if (allocas[i]) {
          builder.setBeforeOp(def);
          builder.create<StoreOp>({ op, allocas[i] }, { new SizeAttr(4) });
        }
      }
      builder.setBeforeOp(ret);
      builder.replace<ContinueOp>(ret);
      def->erase();
    }
  }

  // Fill the before region with "true".
  auto bb = before->appendBlock();
  builder.setToBlockStart(bb);

  auto _true = builder.create<IntOp>({ new IntAttr(1) });
  builder.create<ProceedOp>({ _true });

  // Remove empty blocks introduced this way.
  for (auto bb : bbs) {
    if (bb->getOps().size() == 0)
      bb->erase();
  }
}

void TCO::run() {
  auto funcs = collectFuncs();

  // Pureness.cpp has already built a call graph.
  for (auto func : funcs) {
    const auto &callers = CALLER(func);
    if (std::find(callers.begin(), callers.end(), NAME(func)) == callers.end())
      continue;

    runImpl(func);  
  }
}
