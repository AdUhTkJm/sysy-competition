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
      setName("arm."#Ty); \
    } \
    Ty(Value::Type resultTy): OpImpl(resultTy, {}) { \
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

namespace sys::arm {

// Note that ARM denotes length information on register names, rather than on instruction name.
// We still denote it on instructions; when Dumping, we emit the same opcode but different registers.
// Similarly, the variants of the same instruction is also considered differently.

// Look at here: https://courses.cs.washington.edu/courses/cse469/19wi/arm64.pdf

ARMOP(MovIOp); // Allows a shift amount.
ARMOP(MovkOp); // Keep the immediate and load 16 bytes. Allows a shift amount.
ARMOP(MovnOp); // Load `not immediate`.
ARMOP(MovROp); // To distinguish from loading immediates, an `R` is for moving between registers.

ARMOPF(FmovWOp); // Move from a 32-bit w-register to a fp register.

ARMOPL(AdrOp); // The ADR instruction only allows 1 MB range. We use pseudo-instr `ldr x0, =label` when Dumping.

ARMOP(AddWOp);
ARMOP(AddWIOp); // Accept immediate
ARMOPL(AddXOp);
ARMOPL(AddXIOp); // Accept immediate

ARMOP(SubWOp);
ARMOP(RsbWOp); // Reverse subtract
ARMOP(SubWIOp); // Accept immediate
ARMOP(SubSWOp); // Sub and set flag; Note that only S-suffixed ops will set flag.
ARMOP(SubXOp);

ARMOP(MulWOp);
ARMOPL(MulXOp);

ARMOP(SdivWOp);
ARMOPL(SdivXOp);
ARMOP(UdivWOp);

ARMOP(MlaOp);
ARMOP(MsubWOp); // Multiply-sub: rs3 - rs2 * rs
ARMOP(NegOp);

ARMOPL(SmulhOp);
ARMOPL(UmulhOp);

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
ARMOP(StrWOp); // Store i32
ARMOP(StrXOp); // Store i64
ARMOP(StrFOp); // Store f32

// ==== Take another register, L-shifted by amount (can be 0) ====
// If the l-shift amount is negative, then it means to SUBTRACT THE REGISTER instead.
ARMOP(LdrWROp); // Load i32
ARMOPL(LdrXROp); // Load i64
ARMOPF(LdrFROp); // Load f32
ARMOP(StrWROp); // Store i32
ARMOP(StrXROp); // Store i64
ARMOP(StrFROp); // Store f32

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

ARMOP(CselOp); // xd = cond ? xn : xm
ARMOP(CmpOp); // We do think `cmp` has a result, which is "explicitly" stating the CPSR flags
ARMOP(CmpIOp); // Accept immediate
ARMOP(TstOp); // Same applies to `tst`

// ====== CSET family ======
// Read CPSR flags into a register. Each flag is a different op.
// It takes the result of CmpOp.
ARMOP(CsetNeOp); // Z == 0
ARMOP(CsetEqOp); // Z == 1
ARMOP(CsetLtOp);
ARMOP(CsetLeOp);
ARMOP(CsetGtOp);
ARMOP(CsetGeOp);

// ====== Branch family ======
// Note all of them takes ONE argument. For B-series it's the CPSR flags given by CmpOp,
// and for C-series it's the real argument.
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
ARMOP(FcmpOp);
ARMOP(FmovOp); // Note this is NOT moving between floats; it's fmv.w.x in RISC-V
ARMOP(FcvtzsOp); // f32 -> i32, rounding to zero
ARMOPF(FaddOp);
ARMOPF(FsubOp);
ARMOPF(FmulOp);
ARMOPF(FdivOp);

// ==== Pseudo Ops ====
ARMOP(ReadRegOp);
ARMOP(WriteRegOp);
ARMOP(PlaceHolderOp);
ARMOP(ReloadOp);
ARMOPF(ReloadFOp);
ARMOPL(ReloadLOp);
ARMOP(SpillOp);
ARMOP(SpillFOp);
ARMOP(SpillLOp);
ARMOP(SubSpOp);

inline bool hasRd(Op *op) {
  return !(
    isa<StrWOp>(op) ||
    isa<StrXOp>(op) ||
    isa<StrFOp>(op) ||
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
    isa<CmpOp>(op) ||
    isa<TstOp>(op) ||
    isa<WriteRegOp>(op) ||
    isa<SpillOp>(op) ||
    isa<SpillFOp>(op) ||
    isa<SpillLOp>(op) ||
    isa<SubSpOp>(op)
  );
}


}

#endif
