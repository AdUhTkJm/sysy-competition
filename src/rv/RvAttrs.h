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
  X(a7)

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
  return "unknown";
}

#undef REGS

class RegAttr : public AttrImpl<RegAttr, RVLINE> {
public:
  Reg reg;

  RegAttr(Reg reg): reg(reg) {}

  std::string toString() { return "<reg = " + showReg(reg) + ">"; }
};

class RdAttr : public AttrImpl<RegAttr, RVLINE> {
public:
  Reg reg;

  RdAttr(Reg reg): reg(reg) {}

  std::string toString() { return "<rd = " + showReg(reg) + ">"; }
};

class RsAttr : public AttrImpl<RegAttr, RVLINE> {
public:
  Reg reg;

  RsAttr(Reg reg): reg(reg) {}

  std::string toString() { return "<rs = " + showReg(reg) + ">"; }
};

class Rs2Attr : public AttrImpl<RegAttr, RVLINE> {
public:
  Reg reg;

  Rs2Attr(Reg reg): reg(reg) {}

  std::string toString() { return "<rs2 = " + showReg(reg) + ">"; }
};

}

}

#endif
