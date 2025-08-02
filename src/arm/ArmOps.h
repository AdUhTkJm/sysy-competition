#ifndef ARM_OPS_H
#define ARM_OPS_H

#include "../codegen/OpBase.h"

// Don't forget that we actually rely on OpID, and __LINE__ can duplicate with codegen/Ops.h.
#define ARMOPBASE(ValueTy, Ty) \
  class Ty : public OpImpl<Ty, __LINE__ + 1048576> { \
  public: \
    explicit Ty(const std::vector<Value> &values): OpImpl(ValueTy, values) { \
      setName("arm."#Ty); \
    } \
    Ty(): OpImpl(ValueTy, {}) { \
      setName("arm."#Ty); \
    } \
    explicit Ty(const std::vector<Attr*> &attrs): OpImpl(ValueTy, {}, attrs) { \
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
      setName("arm."#Ty); \
    } \
    explicit Ty(Value::Type resultTy): OpImpl(resultTy, {}) { \
      setName("arm."#Ty); \
    } \
    Ty(Value::Type resultTy, const std::vector<Attr*> &attrs): OpImpl(resultTy, {}, attrs) { \
      setName("arm."#Ty); \
    } \
    Ty(Value::Type resultTy, const std::vector<Value> &values, const std::vector<Attr*> &attrs): OpImpl(resultTy, values, attrs) { \
      setName("arm."#Ty); \
    } \
  }

#define ARMOP(Ty) ARMOPBASE(Value::i32, Ty)
#define ARMOPL(Ty) ARMOPBASE(Value::i64, Ty)
#define ARMOPF(Ty) ARMOPBASE(Value::f32, Ty)
#define ARMOPV(Ty) ARMOPBASE(Value::i128, Ty)

namespace sys::arm {

// Note that ARM denotes length information on register names, rather than on instruction name.
// We still denote it on instructions; when Dumping, we emit the same opcode but different registers.
// Similarly, the variants of the same instruction is also considered differently.

// Look at here: https://courses.cs.washington.edu/courses/cse469/19wi/arm64.pdf

ARMOP(MovIOp); // Allows a shift amount.
ARMOP(MovkOp); // Keep the immediate and load 16 bytes. Allows a shift amount.
ARMOP(MovnOp); // Load `not immediate`.
ARMOP(MovROp); // To distinguish from loading immediates, an `R` is for moving between registers.

ARMOPF(FmovWOp); // Move from a w-register to a fp register.
ARMOP(FmovXOp); // Move from an x-register to a fp register.
ARMOP(FmovDOp); // Move from a 64-bit fp regsiter to a w-register.
ARMOPF(FmovFOp); // Move a floating point immediate to a fp register.

ARMOPL(AdrOp); // The ADR instruction only allows 1 MB range. We use pseudo-instr `ldr x0, =label` when Dumping.

ARMOP(AddWOp);
ARMOP(AddWIOp); // Accept immediate
ARMOP(AddWLOp); // LSL
ARMOP(AddWROp); // LSR
ARMOP(AddWAROp);// ASR
ARMOPL(AddXOp);
ARMOPL(AddXIOp); // Accept immediate
ARMOPL(AddXLOp); // LSL
ARMOPL(AddXROp); // LSR

ARMOP(SubWOp);
ARMOP(SubWIOp); // Accept immediate
ARMOP(SubSWOp); // Sub and set flag; Note that only S-suffixed ops will set flag.
ARMOP(SubXOp);

ARMOP(MulWOp);
ARMOPL(MulXOp);

ARMOP(SdivWOp);
ARMOPL(SdivXOp);
ARMOP(UdivWOp);

ARMOP(MaddWOp); // Multiply-add: rs3 + rs2 * rs
ARMOPL(MaddXOp);
ARMOP(MsubWOp); // Multiply-sub: rs3 - rs2 * rs
ARMOPL(MsubXOp);
ARMOP(NegOp);
ARMOPF(FnegOp);

ARMOPL(SmullOp); // Signed mul long

ARMOP(AndOp);
ARMOP(OrOp);
ARMOP(EorOp); // Xor
ARMOP(AndIOp); // Accept immediate
ARMOP(OrIOp); // Accept immediate
ARMOP(EorIOp); // Accept immediate

// Memory family
// ==== Take an immediate (can be 0) ====
ARMOP(LdrWOp); // Load i32
ARMOPL(LdrXOp); // Load i64
ARMOPF(LdrFOp); // Load f32
ARMOPF(LdrDOp); // Load f64
ARMOP(StrWOp); // Store i32
ARMOP(StrXOp); // Store i64
ARMOP(StrFOp); // Store f32
ARMOP(StrDOp); // Store f64

// ==== Access two registers ====
ARMOP(LdpXOp); // Store i64
ARMOP(LdpDOp); // Store f64
ARMOP(StpXOp); // Store i64
ARMOP(StpDOp); // Store f64

// ==== Take another register, L-shifted by amount (can be 0) ====
ARMOP(LdrWROp); // Load i32
ARMOPL(LdrXROp); // Load i64
ARMOPF(LdrFROp); // Load f32
ARMOP(StrWROp); // Store i32
ARMOP(StrXROp); // Store i64
ARMOP(StrFROp); // Store f32

// ==== Postfix increment ====
ARMOP(LdrWPOp); // Load i32
ARMOPL(LdrXPOp); // Load i64
ARMOPF(LdrFPOp); // Load f32
ARMOP(StrWPOp); // Store i32
ARMOP(StrXPOp); // Store i64
ARMOP(StrFPOp); // Store f32

ARMOP(LslWOp); // // L-shift
ARMOPL(LslXOp); // L-shift
ARMOP(LsrWOp); // Logical r-shift
ARMOPL(LsrXOp); // Logical r-shift
ARMOP(AsrWOp); // Arithmetic r-shift
ARMOPL(AsrXOp); // Arithmetic r-shift

ARMOP(LslWIOp); // L-shift, accept immediate
ARMOPL(LslXIOp); // L-shift, accept immediate
ARMOP(LsrWIOp); // Logical r-shift, accept immediate
ARMOPL(LsrXIOp); // Logical r-shift, accept immediate
ARMOP(AsrWIOp); // Arithmetic r-shift, accept immediate
ARMOPL(AsrXIOp); // Arithmetic r-shift, accept immediate

// ====== CSET family ======
// Read CPSR flags into a register. Each flag is a different op.
// We don't allow CmpOp and similar to appear in itself.
// It implicitly carries a CmpOp with it.
ARMOP(CsetNeOp); // Z == 0
ARMOP(CsetEqOp); // Z == 1
ARMOP(CsetLtOp);
ARMOP(CsetLeOp);
ARMOP(CsetGtOp);
ARMOP(CsetGeOp);

// These carry an FcmpOp.
ARMOP(CsetNeFOp);
ARMOP(CsetEqFOp);
ARMOP(CsetLtFOp);
ARMOP(CsetLeFOp);
ARMOP(CsetGtFOp);
ARMOP(CsetGeFOp);

// These carry a TstOp.
ARMOP(CsetNeTstOp);
ARMOP(CsetEqTstOp);

// These carry an FcmpZOp.
ARMOP(CsetNeFcmpZOp);
ARMOP(CsetEqFcmpZOp);

ARMOP(CnegLtZOp);

// ====== CSEL family ======
// Similar to CSET, CSEL also have different versions.

ARMOP(CselEqZOp);
ARMOP(CselNeZOp);
ARMOP(CselLtZOp);
ARMOP(CselLeZOp);
ARMOP(CselGtZOp);
ARMOP(CselGeZOp);

// ====== Branch family ======
ARMOP(BgtOp);
ARMOP(BleOp);
ARMOP(BeqOp);
ARMOP(BneOp);
ARMOP(BltOp);
ARMOP(BgeOp);
ARMOP(BmiOp); // Branch if minus (< 0)
ARMOP(BplOp); // Branch if plus (> 0)
ARMOP(CbzOp); // Compact branch if zero
ARMOP(CbnzOp); // Compact branch if non-zero

ARMOP(BOp); // Jump
ARMOP(RetOp);
ARMOP(BlOp); // Branch-and-link (jal in RISC-V), so just a call

ARMOPF(ScvtfOp); // i32 -> f32
ARMOP(FmovOp);
ARMOP(FcvtzsOp); // f32 -> i32, rounding to zero
ARMOPF(FaddOp);
ARMOPF(FsubOp);
ARMOPF(FmulOp);
ARMOPF(FdivOp);
ARMOPF(FmaddOp);
ARMOPF(FmsubOp);

// ====== Vector Ops ======
ARMOP(St1Op);
ARMOPV(Ld1Op);
ARMOPV(AddVOp);
ARMOPV(MulVOp);
ARMOPV(MlaVOp);
ARMOPV(DupOp);

// ====== Pseudo Ops ======
ARMOPE(ReadRegOp);
ARMOP(WriteRegOp);
ARMOP(PlaceHolderOp);
ARMOP(SubSpOp);
ARMOP(CloneOp);
ARMOP(JoinOp);
ARMOP(WakeOp);

inline bool hasRd(Op *op) {
  return !(
    isa<StrWOp>(op) ||
    isa<StrXOp>(op) ||
    isa<StrFOp>(op) ||
    isa<StrWPOp>(op) ||
    isa<StrXPOp>(op) ||
    isa<StrFPOp>(op) ||
    isa<StrWROp>(op) ||
    isa<StrXROp>(op) ||
    isa<StrFROp>(op) ||
    isa<St1Op>(op) ||
    isa<BOp>(op) ||
    isa<BlOp>(op) ||
    isa<BeqOp>(op) ||
    isa<BneOp>(op) ||
    isa<BgtOp>(op) ||
    isa<BltOp>(op) ||
    isa<BleOp>(op) ||
    isa<CbzOp>(op) ||
    isa<CbnzOp>(op) ||
    isa<RetOp>(op) ||
    isa<WriteRegOp>(op) ||
    isa<SubSpOp>(op) ||
    isa<CloneOp>(op) ||
    isa<JoinOp>(op) ||
    isa<WakeOp>(op)
  );
}


}

#endif
