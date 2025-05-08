#include "ArmPasses.h"
#include "ArmMatcher.h"

using namespace sys;
using namespace sys::arm;

std::map<std::string, int> InstCombine::stats() {
  return {
    { "combined-ops", combined }
  };
}

static ArmRule rules[] = {
  "(change (addw x (mov #a)) (!only-if (!inbit 12 #a) (addwi x #a)))",
  "(change (addx x (mov #a)) (!only-if (!inbit 12 #a) (addxi x #a)))",
  "(change (cbz (csetlt x) >bb ?bb2) (blt x >bb ?bb2))",
};

void InstCombine::run() {
  auto funcs = collectFuncs();
  int folded;
  do {
    folded = 0;
    for (auto func : funcs) {
      auto region = func->getRegion();

      for (auto bb : region->getBlocks()) {
        auto ops = bb->getOps();
        for (auto op : ops) {
          for (auto &rule : rules) {
            bool success = rule.rewrite(op);
            if (success) {
              folded++;
              break;
            }
          }
        }
      }
    }

    combined += folded;
  } while (folded);
}
