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
  "(change (addw x (lslwi x #a)) (addwl x x #a))",
  "(change (addw (lslwi x #a) x) (addwl x x #a))",
  "(change (addx x (lslwi x #a)) (addxl x x #a))",
  "(change (addx (lslwi x #a) x) (addxl x x #a))",

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
  "(change (ldrf (addxi x #a) #b) (!only-if (!inbit 12 (!add #a #b)) (ldrf x (!add #a #b))))",
  "(change (ldrw (addx x y) #a) (!only-if (!eq #a 0) (ldrwr x y #a)))",
  "(change (ldrx (addx x y) #a) (!only-if (!eq #a 0) (ldrxr x y #a)))",
  "(change (ldrf (addx x y) #a) (!only-if (!eq #a 0) (ldrfr x y #a)))",
  "(change (ldrwr x (lslxi y #a) #b) (!only-if (!eq (!add #a #b) 2) (ldrwr x y 2)))",
  "(change (ldrxr x (lslxi y #a) #b) (!only-if (!eq (!add #a #b) 3) (ldrxr x y 3)))",
  "(change (ldrfr x (lslxi y #a) #b) (!only-if (!eq (!add #a #b) 2) (ldrfr x y 2)))",

  // STR
  "(change (strw y (addxi x #a) #b) (!only-if (!inbit 12 (!add #a #b)) (strw y x (!add #a #b))))",
  "(change (strx y (addxi x #a) #b) (!only-if (!inbit 12 (!add #a #b)) (strx y x (!add #a #b))))",
  "(change (strf y (addxi x #a) #b) (!only-if (!inbit 12 (!add #a #b)) (strf y x (!add #a #b))))",
  "(change (strw z (addx x y) #a) (!only-if (!eq #a 0) (strwr z x y #a)))",
  "(change (strx z (addx x y) #a) (!only-if (!eq #a 0) (strxr z x y #a)))",
  "(change (strf z (addx x y) #a) (!only-if (!eq #a 0) (strfr z x y #a)))",
  "(change (strwr z x (lslxi y #a) #b) (!only-if (!eq (!add #a #b) 2) (strwr z x y 2)))",
  "(change (strxr z x (lslxi y #a) #b) (!only-if (!eq (!add #a #b) 3) (strxr z x y 3)))",
  "(change (strfr z x (lslxi y #a) #b) (!only-if (!eq (!add #a #b) 2) (strfr z x y 2)))",
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
