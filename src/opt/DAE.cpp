#include "CleanupPasses.h"

using namespace sys;

std::map<std::string, int> DAE::stats() {
  return {
    { "removed-arguments", elim },
    { "removed-return-values", elimRet },
  };
}

void DAE::run() {
  // Try to infer constants at each call site.
  auto calls = module->findAll<CallOp>();
  auto fnMap = getFunctionMap();

  // value[fn] gives a map `m`, which:
  //   m[x] = y iff. all calls to `fn` gives `y` to the `x`th argument (counting from 0).
  std::map<FuncOp*, std::map<int, int>> value;
  std::map<FuncOp*, std::set<int>> forbidden;
  // All functions whose return value has been used.
  std::set<FuncOp*> resultUsed;

  for (auto call : calls) {
    FuncOp *fn = fnMap[NAME(call)];
    const auto &operands = call->getOperands();

    if (call->getUses().size() > 0)
      resultUsed.insert(fn);

    for (size_t i = 0; i < operands.size(); i++) {
      auto operand = operands[i];
      auto def = operand.defining;

      if (isa<IntOp>(def)) {
        if (value[fn].count(i) && value[fn][i] == V(def))
          continue;

        // This means the value isn't always the same across all call sites.
        if (value[fn].count(i)) {
          value[fn].erase(i);
          forbidden[fn].insert(i);
          continue;
        }

        if (forbidden[fn].count(i))
          continue;

        value[fn][i] = V(def);
        continue;
      }

      // This is not an integer.
      value[fn].erase(i);
      forbidden[fn].insert(i);
    }
  }

  // Replace GetArgOp for each of the argument in `value`.
  Builder builder;

  auto funcs = collectFuncs();
  for (auto func : funcs) {
    std::set<int, std::greater<int>> toRemove;
    std::set<int> visited;
    int &argcnt = func->get<ArgCountAttr>()->count;

    auto getargs = func->findAll<GetArgOp>();
    const auto &invariant = value[func];

    for (auto getarg : getargs) {
      int index = V(getarg);
      visited.insert(index);

      if (invariant.count(index)) {
        builder.replace<IntOp>(getarg, { new IntAttr(invariant.at(index)) });
        toRemove.insert(index);
        continue;
      }

      // Also check for AliasAttr. 
      // If it always alias the same global with offset 0, then just replace it with GetGlobal.
      if (auto alias = getarg->find<AliasAttr>()) {
        if (alias->location.size() != 1)
          continue;

        const auto &[base, offsets] = *alias->location.begin();
        if (offsets.size() == 1 && offsets[0] == 0 && isa<GlobalOp>(base)) {
          builder.replace<GetGlobalOp>(getarg, { new NameAttr(NAME(base)) });
          toRemove.insert(index);
          continue;
        }
      }
    }

    // Remove those completely unused arguments.
    for (int i = 0; i < argcnt; i++) {
      if (!visited.count(i))
        toRemove.insert(i);
    }

    if (!toRemove.size())
      continue;

    // Decrease argument count of the function.
    argcnt -= toRemove.size();

    // For all remaining getargs, decrease their count as well.
    getargs = func->findAll<GetArgOp>();
    for (auto index : toRemove) {
      // As toRemove is in descending order, this should be correct.
      for (auto getarg : getargs) {
        if (V(getarg) > index)
          V(getarg)--;
      }
    }

    // For all calls, replace their operands.
    const auto &name = NAME(func);
    for (auto call : calls) {
      if (NAME(call) != name)
        continue;

      auto operands = call->getOperands();
      call->removeAllOperands();
      for (size_t i = 0; i < operands.size(); i++) {
        if (toRemove.count(i))
          continue;

        call->pushOperand(operands[i]);
      }
    }

    elim += toRemove.size();

    // Now let's move return values.
    // If the function's return value is never used, then no need to return.
    // (We can't remove `return xx` in main even though it doesn't seem used.)
    if (resultUsed.count(func) || NAME(func) == "main")
      continue;

    auto rets = func->findAll<ReturnOp>();
    for (auto ret : rets)
      ret->removeAllOperands();
  }
}
