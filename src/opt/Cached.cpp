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

      int v = -1;
      // Match `n - 1` for some argument `n`.
      // In fact we have other forms (like `n / 2`),
      // but they are difficult to hit a small precomputed cache.
      for (auto operand : call->getOperands()) {
        auto def = operand.defining;
        if (!minusone.match(def))
          continue;
        auto arg = minusone.extract("x");
        if (!isa<GetArgOp>(arg))
          continue;
        v = V(arg);
      }

      // We must find an inductive argument. The exact argument is not important though.
      if (v == -1)
        continue;

      int argnum = func->get<ArgCountAttr>()->count;
      // Cache too large.
      if (argnum > 3)
        continue;

      exec::Interpreter interp(module);
      if (argnum == 3) {
        int N = 128;
        for (int i = 0; i < N; i++) {
          for (int j = 0; j < N; j++) {
            for (int k = 0; k < N; k++) {
              // Interpret?
            }
          }
        }
      }
    }
  }
}
