#include "Passes.h"
#include "../utils/Matcher.h"
#include "../utils/Exec.h"

using namespace sys;

// Defined in Inline.cpp.
bool isRecursive(Op *op);

namespace {

Rule minusone("(sub x 1)");

}

void Cached::run() {
  // Identify candidate functions.
  auto funcs = collectFuncs();
  for (auto func : funcs) {
    if (!isRecursive(func) || func->has<ImpureAttr>())
      continue;

    const auto &name = NAME(func);

    // Find the induction variables.
    auto calls = func->findAll<CallOp>();
    for (auto call : calls) {
      // We're calling other functions, and it isn't generally possible to emulate.
      if (NAME(call) != name)
        return;
    }
    
    int argnum = func->get<ArgCountAttr>()->count;
    if (argnum > 3 || argnum <= 1)
      continue;

    // Only accept integers.
    auto getargs = func->findAll<GetArgOp>();
    std::vector<Op*> args(argnum);
    for (auto getarg : getargs) {
      if (getarg->getResultType() != Value::i32)
        return;
      args[V(getarg)] = getarg;
    }
    
    auto ret = func->findAll<ReturnOp>();
    if (ret.empty() || ret[0]->DEF()->getResultType() != Value::i32)
      return;

    // Create a cache.
    using namespace exec;
    Interpreter interp(module);
    Builder builder;
    builder.setToRegionStart(module->getRegion());
    const auto cachename = "__cache_" + name;
    if (argnum == 3) {
      auto vi = new cache_3;
      interp.useCache(vi);
      for (int i = 0; i < CACHE_3_N; i++) {
        for (int j = 0; j < CACHE_3_N; j++) {
          for (int k = 0; k < CACHE_3_N; k++)
            interp.runFunction(name, { i, j, k });
        }
      }
      builder.create<GlobalOp>({ new NameAttr(cachename),
        new IntArrayAttr((int*) vi, CACHE_3_TOTAL),
        new SizeAttr(CACHE_3_TOTAL * 4)
      });
    }

    if (argnum == 2) {
      auto vi = new cache_2;
      interp.useCache(vi);
      for (int i = 0; i < CACHE_2_N; i++) {
        for (int j = 0; j < CACHE_2_N; j++)
          interp.runFunction(name, { i, j });
      }
      builder.create<GlobalOp>({ new NameAttr(cachename),
        new IntArrayAttr((int*) vi, CACHE_2_TOTAL),
        new SizeAttr(CACHE_2_TOTAL * 4)
      });
    }

    // Utilize the cache.
    auto region = func->getRegion();
    auto first = region->getFirstBlock();
    builder.setToBlockStart(first);

    // Find the last `getarg`.
    auto insert = first->getFirstOp();
    const auto &ops = first->getOps();
    for (auto it = ops.rbegin(); it != ops.rend(); it++) {
      auto op = *it;
      if (isa<GetArgOp>(op)) {
        insert = op->nextOp();
        break;
      }
    }

    // Insert an `if` check for it.
    std::vector<Op*> inrange;
    builder.setBeforeOp(insert);
    int size = argnum == 2 ? CACHE_2_N : CACHE_3_N;
    auto max = builder.create<IntOp>({ new IntAttr(size) });
    auto zero = builder.create<IntOp>({ new IntAttr(0) });
    for (auto arg : getargs) {
      auto lt = builder.create<LtOp>({ arg, max });
      auto ge = builder.create<LeOp>({ zero, arg });
      inrange.push_back(lt);
      inrange.push_back(ge);
    }

    // Chain them with `and`.
    auto cond = inrange[0];
    for (int i = 1; i < inrange.size(); i++)
      cond = builder.create<AndIOp>({ cond, inrange[i]->getResult() });

    // Create a branch to check cache hit.
    auto body = region->insertAfter(first);
    auto exit = region->insertAfter(body);
    first->splitOpsAfter(exit, cond);
    cond->moveToEnd(first); // `cond` itself will also be moved over.
    builder.setToBlockEnd(first);
    builder.create<BranchOp>({ cond }, { new TargetAttr(body), new ElseAttr(exit) });

    // On `body`, retrieve the cache content.
    builder.setToBlockStart(body);
    auto addr = builder.create<GetGlobalOp>({ new NameAttr(cachename) });
    auto four = builder.create<IntOp>({ new IntAttr(4) });
    if (argnum == 3) {
      auto stride1 = builder.create<IntOp>({ new IntAttr(size * size * 4) });
      auto stride2 = builder.create<IntOp>({ new IntAttr(size * 4) });
      
      auto mul1 = builder.create<MulIOp>({ stride1, args[0] });
      auto mul2 = builder.create<MulIOp>({ stride2, args[1] });
      auto mul3 = builder.create<MulIOp>({ four, args[2] });

      auto add1 = builder.create<AddLOp>({ addr, mul1 });
      auto add2 = builder.create<AddIOp>({ mul2, mul3->getResult() });
      auto add3 = builder.create<AddLOp>({ add1, add2 });

      auto value = builder.create<LoadOp>(Value::i32, { add3 }, { new SizeAttr(4) });
      builder.create<ReturnOp>({ value });
    }

    if (argnum == 2) {
      // TODO
    }
  }
}
