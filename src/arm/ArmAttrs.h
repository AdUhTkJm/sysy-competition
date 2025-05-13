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

#define RATTR(Ty) \
  class Ty : public AttrImpl<Ty, ARMLINE> { \
  public: \
    Reg reg; \
    Ty(Reg reg): reg(reg) {} \
    std::string toString() { return "<" + showReg(reg) + ">"; } \
    Ty *clone() { return new Ty(reg); } \
  };

#define RPOLYATTR(Ty) \
  class Ty : public AttrImpl<Ty, ARMLINE> { \
  public: \
    int offset; \
    Value::Type ty; \
    Ty(int offset, Value::Type ty): offset(offset), ty(ty) {} \
    std::string toString() { return "<" + std::to_string(offset) + ">"; } \
    Ty *clone() { return new Ty(offset, ty); } \
  };

RATTR(RegAttr);
RPOLYATTR(RdAttr);
RPOLYATTR(RsAttr);
RPOLYATTR(Rs2Attr);
RPOLYATTR(Rs3Attr);

}
  
}

#define STACKOFF(op) (op)->get<StackOffsetAttr>()->offset
#define REG(op) (op)->get<RegAttr>()->reg
#define RDOFF(op) (op)->get<RdAttr>()->offset
#define RSOFF(op) (op)->get<RsAttr>()->offset
#define RS2OFF(op) (op)->get<Rs2Attr>()->offset
#define RS3OFF(op) (op)->get<Rs3Attr>()->offset
#define RD(op) ((Reg) RDOFF(op))
#define RS(op) ((Reg) RSOFF(op))
#define RS2(op) ((Reg) RS2OFF(op))
#define RS3(op) ((Reg) RS3OFF(op))

#endif
