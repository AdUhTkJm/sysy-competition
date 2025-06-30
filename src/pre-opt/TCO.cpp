#include "PrePasses.h"

using namespace sys;

std::map<std::string, int> TCO::stats() {
  return {
    { "removed-calls", uncalled },
  };
}

bool TCO::runImpl(FuncOp *func) {
  const auto &name = NAME(func);
  
  // Try to identify whether `func` is tail recursive.
  auto rets = func->findAll<ReturnOp>();
  // Whether a return uses the result of `call`.
  // If not, then there's no need to perform TCO even though it's correct to do.
  bool hasCallRet = false;

  for (auto ret : rets) {
    if (!ret->getOperandCount())
      return false;

    auto def = ret->DEF();
    if (isa<CallOp>(def)) {
      const auto &callname = NAME(def);
      if (callname != name || def != ret->prevOp())
        return false;
      hasCallRet = true;
    }

    // Other operations aren't important.
  }

  if (!hasCallRet)
    return false;

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
    auto def = ret->DEF();

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
    if (bb->getOpCount() == 0)
      bb->erase();
  }
  return true;
}

// We can also tail-optimize functions returning `x + func()`.
// Technically it's called "tail optimization modulo commutative monoid",
// but there's no need of doing CPS -> Defunctionalize -> Reassociate etc:
// we don't really need the general form.
//
// int f(int x) {
//   if (x <= 1)
//     return 1;
//   return f(x - 1) + f(x - 2);
// }
//
// int f(int x, int acc) {
//   while (true) {
//     if (x <= 1)
//       return acc + 1;
//     acc += f(x - 1, acc);
//     x -= 2;
//   }
// } 
bool TCO::runAdd(FuncOp *func) {
  const auto &name = NAME(func);
  
  auto rets = func->findAll<ReturnOp>();
  Op *continuation = nullptr, *replace = nullptr, *accret = nullptr;
  for (auto ret : rets) {
    if (!ret->getOperandCount())
      return false;

    auto def = ret->DEF();
    if (isa<AddIOp>(def)) {
      auto x = def->DEF(0), y = def->DEF(1);
      if (isa<CallOp>(x)) {
        const auto &callname = NAME(x);
        if (continuation || callname != name || def != ret->prevOp() || def->getUses().size() > 1)
          return false;
        continuation = y;
        replace = x;
        accret = ret;
      }
    }
  }

  if (!continuation)
    return false;

  uncalled++;
  auto region = func->getRegion();
  
  Builder builder;

  // Put the whole function body inside a WhileOp.
  auto tail = region->appendBlock();
  builder.setToBlockStart(tail);

  // Surround the body into a while.
  auto loop = builder.create<WhileOp>();
  auto before = loop->appendRegion();
  auto after = loop->appendRegion();
  auto afterEntry = after->appendBlock();

  auto bbs = region->getBlocks();
  for (auto bb : bbs) {
    if (bb != tail)
      bb->moveAfter(afterEntry);
  }
  
  // Create an accumulator, and initialize it to zero.
  builder.setBeforeOp(loop);
  auto accum = builder.create<AllocaOp>({ new SizeAttr(4) });
  auto zero = builder.create<IntOp>({ new IntAttr(0) });
  builder.create<StoreOp>({ zero, accum });

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

  // Replace all other calls; they should be added with `accum`.
  for (auto ret : rets) {
    auto def = ret->DEF();
    if (isa<AddIOp>(def) && def->DEF(0) == replace)
      continue;

    builder.setBeforeOp(ret);
    auto acc = builder.create<LoadOp>(Value::i32, { accum }, { new SizeAttr(4) });
    auto w = builder.create<AddIOp>({ acc, def });
    ret->setOperand(0, w);
  }

  // Replace the call of addition.
  builder.setToBlockStart(newEntry);
  for (int i = 0; i < replace->getOperandCount(); i++) {
    Value op = replace->getOperand(i);
    if (allocas[i]) {
      builder.setBeforeOp(accret);
      builder.create<StoreOp>({ op, allocas[i] }, { new SizeAttr(4) });
    }
  }
  // Create a sum.
  builder.setBeforeOp(accret);
  auto acc = builder.create<LoadOp>(Value::i32, { accum }, { new SizeAttr(4) });
  auto w = builder.create<AddIOp>({ acc, continuation });
  builder.create<StoreOp>({ w, accum }, { new SizeAttr(4) });
  
  auto def = accret->DEF();
  builder.replace<ContinueOp>(accret);
  def->erase();
  replace->erase();

  // Fill the before region with "true".
  auto bb = before->appendBlock();
  builder.setToBlockStart(bb);

  auto _true = builder.create<IntOp>({ new IntAttr(1) });
  builder.create<ProceedOp>({ _true });

  // Remove empty blocks introduced this way.
  for (auto bb : bbs) {
    if (bb->getOpCount() == 0)
      bb->erase();
  }
  return true;
}

void TCO::run() {
  auto funcs = collectFuncs();

  // Pureness.cpp has already built a call graph.
  for (auto func : funcs) {
    const auto &callers = CALLER(func);
    if (std::find(callers.begin(), callers.end(), NAME(func)) == callers.end())
      continue;

    // This will result in a lot of spilled registers.
    // Don't do that - keep them inside a function might be a better idea.
    if (func->get<ArgCountAttr>()->count >= 16)
      continue;

    if (!runImpl(func))
      runAdd(func);
  }
}
