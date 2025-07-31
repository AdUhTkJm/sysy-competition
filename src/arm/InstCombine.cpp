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
  "(change (addw x (lslwi y #a)) (addwl x y #a))",
  "(change (addw (lslwi y #a) x) (addwl x y #a))",
  "(change (addx x (lslwi y #a)) (addxl x y #a))",
  "(change (addx (lslwi y #a) x) (addxl x y #a))",
  "(change (addw x (lslxi y #a)) (addwl x y #a))",
  "(change (addw (lslxi y #a) x) (addwl x y #a))",
  "(change (addx x (lslxi y #a)) (addxl x y #a))",
  "(change (addx (lslxi y #a) x) (addxl x y #a))",
  "(change (addw x (asrwi y #a)) (addwar x y #a))",
  "(change (addw (asrwi y #a) x) (addwar x y #a))",
  "(change (addw (mulw x y) z) (maddw x y z))",
  "(change (addw z (mulw x y)) (maddw x y z))",

  // FADD, FSUB: precision changes unexpectedly
  // "(change (fadd (fmul x y) z) (fmadd x y z))",
  // "(change (fadd z (fmul x y)) (fmadd x y z))",

  // SUB
  "(change (subw x (mov #a)) (!only-if (!inbit 12 (!minus #a)) (addwi x (!minus #a))))",
  "(change (subx x (mov #a)) (!only-if (!inbit 12 (!minus #a)) (addxi x (!minus #a))))",

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
