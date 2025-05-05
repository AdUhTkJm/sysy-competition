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

class RegAttr : public AttrImpl<RegAttr, RVLINE> {
public:
  Reg reg;

  RegAttr(Reg reg): reg(reg) {}

  std::string toString() { return "<reg = " + showReg(reg) + ">"; }
  RegAttr *clone() { return new RegAttr(reg); }
};

class RdAttr : public AttrImpl<RegAttr, RVLINE> {
public:
  Reg reg;

  RdAttr(Reg reg): reg(reg) {}

  std::string toString() { return "<rd = " + showReg(reg) + ">"; }
  RdAttr *clone() { return new RdAttr(reg); }
};

class RsAttr : public AttrImpl<RegAttr, RVLINE> {
public:
  Reg reg;

  RsAttr(Reg reg): reg(reg) {}

  std::string toString() { return "<rs = " + showReg(reg) + ">"; }
  RsAttr *clone() { return new RsAttr(reg); }
};

class Rs2Attr : public AttrImpl<RegAttr, RVLINE> {
public:
  Reg reg;

  Rs2Attr(Reg reg): reg(reg) {}

  std::string toString() { return "<rs2 = " + showReg(reg) + ">"; }
  Rs2Attr *clone() { return new Rs2Attr(reg); }
};

// Marked to GetArgOp, denoting that it should be passed via a register.
// Used in RegAlloc to determine which register should be preserved.
class PassByRegAttr : public AttrImpl<PassByRegAttr, RVLINE> {
public:
  Reg reg;

  PassByRegAttr(Reg reg): reg(reg) {}

  std::string toString() { return "<pass by " + showReg(reg) + ">"; }
  PassByRegAttr *clone() { return new PassByRegAttr(reg); }
};

}

}

#endif
