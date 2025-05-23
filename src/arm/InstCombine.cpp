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
  // ADD
  "(change (addw x (mov #a)) (!only-if (!inbit 12 #a) (addwi x #a)))",
  "(change (addx x (mov #a)) (!only-if (!inbit 12 #a) (addxi x #a)))",

  // CBZ
  "(change (cbz (csetlt x) >ifso >ifnot) (blt x >ifnot >ifso))",
  "(change (cbz (csetne x) >ifso >ifnot) (beq x >ifso >ifnot))",
  "(change (cbz (cseteq x) >ifso >ifnot) (bne x >ifso >ifnot))",

  // CBNZ
  "(change (cbnz (csetlt x) >ifso >ifnot) (blt x >ifso >ifnot))",
  "(change (cbnz (csetne x) >ifso >ifnot) (bne x >ifso >ifnot))",
  "(change (cbnz (cseteq x) >ifso >ifnot) (beq x >ifso >ifnot))",

  // CMP
  "(change (cmp x (mov #a)) (!only-if (!inbit 12 #a) (cmpi x #a)))",

  // Meaning: jump to `ifso` if x == 0.
  "(change (beq (tst x x) >ifso >ifnot) (cbz x >ifso >ifnot))",
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
