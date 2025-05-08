#include "ArmPasses.h"

using namespace sys::arm;

static const Reg fregs[] = {
  Reg::v0, Reg::v1, Reg::v2, Reg::v3,
  Reg::v4, Reg::v5, Reg::v6, Reg::v7,
};
static const Reg regs[] = {
  Reg::x0, Reg::x1, Reg::x2, Reg::x3,
  Reg::x4, Reg::x5, Reg::x6, Reg::x7,
};

void RegAlloc::run() {

}
