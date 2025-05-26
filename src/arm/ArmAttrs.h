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
  /* x31: either sp or zero, based on context; we consider it as two separate ones */ \
  X(sp) \
  X(xzr) \
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

inline std::ostream &operator<<(std::ostream &os, Reg reg) {
  return os << showReg(reg);
}

#undef REGS

inline bool isFP(Reg reg) {
  return (int) Reg::v0 <= (int) reg && (int) Reg::v31 >= (int) reg;
}

class StackOffsetAttr : public AttrImpl<StackOffsetAttr, ARMLINE> {
public:
  int offset;

  StackOffsetAttr(int offset): offset(offset) {}
  
  std::string toString() { return "<offset = " + std::to_string(offset) + ">"; }
  StackOffsetAttr *clone() { return new StackOffsetAttr(offset); }
};

class LslAttr : public AttrImpl<LslAttr, ARMLINE> {
public:
  int vi;

  LslAttr(int vi): vi(vi) {}
  
  std::string toString() override { return "<lsl = " + std::to_string(vi) + ">"; }
  LslAttr *clone() override { return new LslAttr(vi); }
};

#define RATTR(Ty, name) \
  class Ty : public AttrImpl<Ty, ARMLINE> { \
  public: \
    Reg reg; \
    Ty(Reg reg): reg(reg) {} \
    std::string toString() override { return "<" name + showReg(reg) + ">"; } \
    Ty *clone() override { return new Ty(reg); } \
  };

RATTR(RegAttr, "");
RATTR(RdAttr, "rd = ");
RATTR(RsAttr, "rs = ");
RATTR(Rs2Attr, "rs2 = ");
RATTR(Rs3Attr, "rs3 = ");

}
  
}

#define STACKOFF(op) (op)->get<StackOffsetAttr>()->offset
#define REG(op) (op)->get<RegAttr>()->reg
#define RD(op) (op)->get<RdAttr>()->reg
#define RS(op) (op)->get<RsAttr>()->reg
#define RS2(op) (op)->get<Rs2Attr>()->reg
#define RS3(op) (op)->get<Rs3Attr>()->reg
#define RDC(x) new RdAttr(x)
#define RSC(x) new RsAttr(x)
#define RS2C(x) new Rs2Attr(x)
#define RS3C(x) new Rs3Attr(x)
#define LSL(op) (op)->get<LslAttr>()->vi

#endif
