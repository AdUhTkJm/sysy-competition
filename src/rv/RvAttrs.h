#ifndef RVATTRS_H
#define RVATTRS_H

#include "../codegen/OpBase.h"
#include <string>
#define RVLINE __LINE__ + 524288

namespace sys {

namespace rv {

#define REGS \
  X(zero) \
  X(ra) \
  X(sp) \
  X(gp) \
  X(tp) \
  X(t0) \
  X(t1) \
  X(t2) \
  X(t3) \
  X(t4) \
  X(t5) \
  X(t6) \
  X(s0) \
  X(s1) \
  X(s2) \
  X(s3) \
  X(s4) \
  X(s5) \
  X(s6) \
  X(s7) \
  X(s8) \
  X(s9) \
  X(s10) \
  X(s11) \
  X(a0) \
  X(a1) \
  X(a2) \
  X(a3) \
  X(a4) \
  X(a5) \
  X(a6) \
  X(a7) \
  X(ft0) \
  X(ft1) \
  X(ft2) \
  X(ft3) \
  X(ft4) \
  X(ft5) \
  X(ft6) \
  X(ft7) \
  X(ft8) \
  X(ft9) \
  X(ft10) \
  X(ft11) \
  X(fs0) \
  X(fs1) \
  X(fs2) \
  X(fs3) \
  X(fs4) \
  X(fs5) \
  X(fs6) \
  X(fs7) \
  X(fs8) \
  X(fs9) \
  X(fs10) \
  X(fs11) \
  X(fa0) \
  X(fa1) \
  X(fa2) \
  X(fa3) \
  X(fa4) \
  X(fa5) \
  X(fa6) \
  X(fa7)

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
  return (int) Reg::ft0 <= (int) reg && (int) Reg::fa7 >= (int) reg;
}

class RegAttr : public AttrImpl<RegAttr, RVLINE> {
public:
  Reg reg;

  RegAttr(Reg reg): reg(reg) {}

  std::string toString() override { return "<reg = " + showReg(reg) + ">"; }
  RegAttr *clone() override { return new RegAttr(reg); }
};

class RdAttr : public AttrImpl<RegAttr, RVLINE> {
public:
  Reg reg;

  RdAttr(Reg reg): reg(reg) {}

  std::string toString() override { return "<rd = " + showReg(reg) + ">"; }
  RdAttr *clone() override { return new RdAttr(reg); }
};

class RsAttr : public AttrImpl<RegAttr, RVLINE> {
public:
  Reg reg;

  RsAttr(Reg reg): reg(reg) {}

  std::string toString() override { return "<rs = " + showReg(reg) + ">"; }
  RsAttr *clone() override { return new RsAttr(reg); }
};

class Rs2Attr : public AttrImpl<RegAttr, RVLINE> {
public:
  Reg reg;

  Rs2Attr(Reg reg): reg(reg) {}

  std::string toString() override { return "<rs2 = " + showReg(reg) + ">"; }
  Rs2Attr *clone() override { return new Rs2Attr(reg); }
};

// Stack offset from bp.
class StackOffsetAttr : public AttrImpl<StackOffsetAttr, RVLINE> {
public:
  int offset;

  StackOffsetAttr(int offset): offset(offset) {}

  std::string toString() override { return "<offset = " + std::to_string(offset) + ">"; }
  StackOffsetAttr *clone() override { return new StackOffsetAttr(offset); }
};

}

#define STACKOFF(op) (op)->get<StackOffsetAttr>()->offset
#define RD(op) (op)->get<RdAttr>()->reg
#define RS(op) (op)->get<RsAttr>()->reg
#define RS2(op) (op)->get<Rs2Attr>()->reg
#define REG(op) (op)->get<RegAttr>()->reg
#define RDC(x) new RdAttr(x)
#define RSC(x) new RsAttr(x)
#define RS2C(x) new Rs2Attr(x)

}

#endif
