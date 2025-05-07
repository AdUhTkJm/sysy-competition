#ifndef ARM_ATTRS_H
#define ARM_ATTRS_H

#include "../codegen/Attrs.h"
#define ARMLINE __LINE__ + 1048576

namespace sys {

namespace arm {

#define REGS \
  /* x0 - x7: arguments */ \
  X(x0) \
  X(x1) \
  X(x2) \
  X(x3) \
  X(x4) \
  X(x5) \
  X(x6) \
  X(x7) \
  /* x8: indirect result (we don't need it) */ \
  X(x8) \
  /* x9 - x15: caller saved (temps) */ \
  X(x9) \
  X(x10) \
  X(x11) \
  X(x12) \
  X(x13) \
  X(x14) \
  X(x15) \
  /* x16 - x18: reserved. Avoid them. */ \
  X(x16) \
  X(x17) \
  X(x18) \
  /* x19 - x29: callee saved. (x29 can be `fp`) */ \
  X(x19) \
  X(x20) \
  X(x21) \
  X(x22) \
  X(x23) \
  X(x24) \
  X(x25) \
  X(x26) \
  X(x27) \
  X(x28) \
  X(x29) \
  /* x30: ra */ \
  X(x30) \
  /* x31: either sp or zero, based on context */ \
  X(x31) \
  /* v0 - v7: arguments */ \
  X(v0) \
  X(v1) \
  X(v2) \
  X(v3) \
  X(v4) \
  X(v5) \
  X(v6) \
  X(v7) \
  /* v8 - v15: caller saved (temps) */ \
  X(v8) \
  X(v9) \
  X(v10) \
  X(v11) \
  X(v12) \
  X(v13) \
  X(v14) \
  X(v15) \
  /* v16 - v31: callee saved */ \
  X(v16) \
  X(v17) \
  X(v18) \
  X(v19) \
  X(v20) \
  X(v21) \
  X(v22) \
  X(v23) \
  X(v24) \
  X(v25) \
  X(v26) \
  X(v27) \
  X(v28) \
  X(v29) \
  X(v30) \
  X(v31)

#define X(name) name,
enum class Reg : signed int {
  REGS
};

#undef X

inline std::string showReg(Reg reg) {
  switch (reg) {
#define X(name) case Reg::name: return #name;
    REGS
#undef X
  }
  return "<unknown = " + std::to_string((int) reg) + ">";
}

#undef REGS

inline bool isFP(Reg reg) {
  return (int) Reg::v0 <= (int) reg && (int) Reg::v31 >= (int) reg;
}

class RegAttr : public AttrImpl<RegAttr, ARMLINE> {
public:
  Reg reg;

  RegAttr(Reg reg): reg(reg) {}

  std::string toString() { return "<reg = " + showReg(reg) + ">"; }
  RegAttr *clone() { return new RegAttr(reg); }
};

class RdAttr : public AttrImpl<RegAttr, ARMLINE> {
public:
  Reg reg;

  RdAttr(Reg reg): reg(reg) {}

  std::string toString() { return "<rd = " + showReg(reg) + ">"; }
  RdAttr *clone() { return new RdAttr(reg); }
};

class RsAttr : public AttrImpl<RegAttr, ARMLINE> {
public:
  Reg reg;

  RsAttr(Reg reg): reg(reg) {}

  std::string toString() { return "<rs = " + showReg(reg) + ">"; }
  RsAttr *clone() { return new RsAttr(reg); }
};

class Rs2Attr : public AttrImpl<RegAttr, ARMLINE> {
public:
  Reg reg;

  Rs2Attr(Reg reg): reg(reg) {}

  std::string toString() { return "<rs2 = " + showReg(reg) + ">"; }
  Rs2Attr *clone() { return new Rs2Attr(reg); }
};

}
  
}

#endif
