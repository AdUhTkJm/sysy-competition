#include "ArmPasses.h"
#include "ArmMatcher.h"

using namespace sys;
using namespace sys::arm;

std::map<std::string, int> InstCombine::stats() {
  return {
    { "combined-ops", combined }
  };
}

ArmRule rules[] = {

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
