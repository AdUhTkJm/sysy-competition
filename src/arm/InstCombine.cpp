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

  // SUB
  "(change (subw x (mov #a)) (!only-if (!inbit 12 (!minus #a)) (addwi x (!minus #a))))",

  // CBZ
  "(change (cbz (csetlt x y) >ifso >ifnot) (blt x y >ifnot >ifso))",
  "(change (cbz (csetle x y) >ifso >ifnot) (ble x y >ifnot >ifso))",
  "(change (cbz (csetne x y) >ifso >ifnot) (beq x y >ifso >ifnot))",
  "(change (cbz (cseteq x y) >ifso >ifnot) (bne x y >ifso >ifnot))",

  // CBNZ
  "(change (cbnz (csetlt x y) >ifso >ifnot) (blt x y >ifso >ifnot))",
  "(change (cbnz (csetle x y) >ifso >ifnot) (ble x y >ifso >ifnot))",
  "(change (cbnz (csetne x y) >ifso >ifnot) (bne x y >ifso >ifnot))",
  "(change (cbnz (cseteq x y) >ifso >ifnot) (beq x y >ifso >ifnot))",

  // LDR
  "(change (ldrw (addxi x #a) #b) (!only-if (!inbit 12 (!add #a #b)) (ldrw x (!add #a #b))))",
  "(change (ldrx (addxi x #a) #b) (!only-if (!inbit 12 (!add #a #b)) (ldrx x (!add #a #b))))",

  // STR
  "(change (strw y (addxi x #a) #b) (!only-if (!inbit 12 (!add #a #b)) (strw y x (!add #a #b))))",
  "(change (strx y (addxi x #a) #b) (!only-if (!inbit 12 (!add #a #b)) (strx y x (!add #a #b))))",
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
