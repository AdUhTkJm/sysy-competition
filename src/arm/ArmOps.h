#ifndef ARM_OPS_H
#define ARM_OPS_H

#include "../codegen/OpBase.h"

// Don't forget that we actually rely on OpID, and __LINE__ can duplicate with codegen/Ops.h.
#define ARMOPBASE(ValueTy, Ty) \
  class Ty : public OpImpl<Ty, __LINE__ + 1048576> { \
  public: \
    Ty(const std::vector<Value> &values): OpImpl(ValueTy, values) { \
      setName("arm."#Ty); \
    } \
    Ty(): OpImpl(ValueTy, {}) { \
      setName("arm."#Ty); \
    } \
    Ty(const std::vector<Attr*> &attrs): OpImpl(ValueTy, {}, attrs) { \
      setName("arm."#Ty); \
    } \
    Ty(const std::vector<Value> &values, const std::vector<Attr*> &attrs): OpImpl(ValueTy, values, attrs) { \
      setName("arm."#Ty); \
    } \
  }

#define ARMOPE(Ty) \
  class Ty : public OpImpl<Ty, __LINE__ + 1048576> { \
  public: \
    Ty(Value::Type resultTy, const std::vector<Value> &values): OpImpl(resultTy, values) { \
      setName("rv."#Ty); \
    } \
    Ty(Value::Type resultTy): OpImpl(resultTy, {}) { \
      setName("rv."#Ty); \
    } \
    Ty(Value::Type resultTy, const std::vector<Attr*> &attrs): OpImpl(resultTy, {}, attrs) { \
      setName("rv."#Ty); \
    } \
    Ty(Value::Type resultTy, const std::vector<Value> &values, const std::vector<Attr*> &attrs): OpImpl(resultTy, values, attrs) { \
      setName("rv."#Ty); \
    } \
  }

#define ARMOP(Ty) ARMOPBASE(Value::i32, Ty)
#define ARMOPL(Ty) ARMOPBASE(Value::i64, Ty)
#define ARMOPF(Ty) ARMOPBASE(Value::f32, Ty)

namespace sys {

namespace arm {

// Note that ARM denotes length information on register names, rather than on instruction name.
ARMOP(MovOp);
ARMOP(AdrOp); // The ADR instruction only allows 1 MB range. We use pseudo-instr `ldr x0, =label` when Dumping.
ARMOP(AddOp);
ARMOP(SubOp);
ARMOP(SubSOp); // Sub and set flag
ARMOP(MulOp);
ARMOP(SdivOp);
ARMOP(UdivOp);
ARMOP(MlaOp);
ARMOP(NegOp);
ARMOP(SmulhOp);
ARMOP(UmulhOp);
ARMOP(AndOp);
ARMOP(OrOp);
ARMOP(EorOp); // Xor
ARMOP(LdrOp); // Load
ARMOP(LslOp); // l-shift
ARMOP(LsrOp); // Logical r-shift
ARMOP(AsrOp); // Arithmetic r-shift
ARMOP(StrOp); // Store
ARMOP(CselOp); // csel xd, xn, xm, cond; meaning: xd = cond ? xn : xm
ARMOP(CmpOp);
ARMOP(CsetOp); // set flags according to cmp
ARMOP(BgtOp);
ARMOP(BleOp);
ARMOP(BeqOp);
ARMOP(BneOp);
ARMOP(BltOp);
ARMOP(BOp); // Jump
ARMOP(RetOp);
ARMOP(BrOp); // Branch-and-link (jal in RISC-V), so just a call
ARMOPF(ScvtfOp); // i32 -> f32
ARMOP(FcmpOp);
ARMOP(FmovOp); // Note this is NOT moving between floats; it's fmv.w.x in RISC-V
ARMOP(FcvtzsOp); // f32 -> i32, rounding to zero
ARMOPF(FaddOp);
ARMOPF(FsubOp);
ARMOPF(FmulOp);
ARMOPF(FdivOp);

inline bool hasRd(Op *op) {
  return !(
    isa<StrOp>(op) ||
    isa<BOp>(op) ||
    isa<BrOp>(op) ||
    isa<BeqOp>(op) ||
    isa<BneOp>(op) ||
    isa<BgtOp>(op) ||
    isa<BltOp>(op) ||
    isa<BleOp>(op) ||
    isa<RetOp>(op)
  );
}


}

}

#endif
