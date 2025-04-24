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
enum class Regs {
  REGS
};

#undef X

inline std::string showReg(Regs reg) {
  switch (reg) {
#define X(name) case Regs::name: return #name;
    REGS
#undef X
  }
  return "unknown";
}

#undef REGS

class RegAttr : public AttrImpl<RegAttr, RVLINE> {
public:
  Regs reg;

  RegAttr(Regs reg): reg(reg) {}

  std::string toString() { return "<reg = " + showReg(reg) + ">"; }
};

class OffsetAttr : public AttrImpl<OffsetAttr, RVLINE> {
public:
  int offset;

  OffsetAttr(int offset): offset(offset) {}

  std::string toString() { return "<offset = " + std::to_string(offset) + ">"; }
};

}

}

#endif
