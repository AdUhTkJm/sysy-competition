#ifndef RV_OPS_H
#define RV_OPS_H

#include "../codegen/Ops.h"

// Don't forget that we actually rely on OpID, and __LINE__ can duplicate with codegen/Ops.h.
#define RVOPBASE(ValueTy, Ty) \
  class Ty : public OpImpl<Ty, __LINE__ + 524288> { \
  public: \
    Ty(const std::vector<Value> &values): OpImpl(ValueTy, values) { \
      setName("rv."#Ty); \
    } \
    Ty(): OpImpl(ValueTy, {}) { \
      setName("rv."#Ty); \
    } \
    Ty(const std::vector<Attr*> &attrs): OpImpl(ValueTy, {}, attrs) { \
      setName("rv."#Ty); \
    } \
    Ty(const std::vector<Value> &values, const std::vector<Attr*> &attrs): OpImpl(ValueTy, values, attrs) { \
      setName("rv."#Ty); \
    } \
  }

// Ops that must be explicitly set a result type.
#define RVOPE(Ty) \
  class Ty : public OpImpl<Ty, __LINE__ + 524288> { \
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

#define RVOP(Ty) RVOPBASE(Value::i32, Ty)
#define RVOPL(Ty) RVOPBASE(Value::i64, Ty)
#define RVOPF(Ty) RVOPBASE(Value::f32, Ty)

namespace sys {

namespace rv {

// To add an op:
//    1) Check RegAlloc.cpp, the list of LOWER(...)
//    2) Check the function hasRd().

RVOP(LiOp);
RVOPL(LaOp);
RVOPL(AddOp);
RVOP(AddwOp);
RVOP(AddiwOp);
RVOPL(AddiOp); // Note that pointers can't be `addiw`'d.
RVOPL(SubOp);
RVOP(SubwOp);
RVOP(MulwOp);
RVOPL(MulOp);
RVOP(DivwOp); // Signed; divu for unsigned.
RVOPL(DivOp);
RVOP(SlliwOp); // Shift left.
RVOP(SrliwOp); // Shift right, unsigned.
RVOPL(SraiOp); // Shift right (64 bit), signed.
RVOP(SraiwOp); // Shift right, signed.
RVOP(MulhOp); // Higher bits of mul, signed.
RVOP(MulhuOp); // Higher bits of mul, unsigned.
RVOP(AndOp);
RVOP(OrOp);
RVOP(XorOp);
RVOP(AndiOp);
RVOP(OriOp);
RVOP(XoriOp);
RVOP(BneOp);
RVOP(BeqOp);
RVOP(BltOp);
RVOP(BgeOp);
RVOP(SeqzOp); // Set equal to zero (pseudo, = sltiu)
RVOP(SnezOp); // Set not equal to zero (pseudo, 2 ops)
RVOP(SltOp); // Set less than
RVOP(SltiOp);
RVOP(JOp);
RVOP(MvOp);
RVOP(RetOp);
RVOPE(LoadOp);
RVOP(StoreOp);
RVOP(SubSpOp); // Allocate stack space: sub sp, sp, <IntAttr>
RVOPE(ReadRegOp); // Read from real register
RVOP(WriteRegOp); // Write to real register; the SSA value is used and pre-colored in RegAlloc.
RVOP(CallOp);
RVOP(PlaceHolderOp); // See regalloc; holds a place to denote a register isn't available.
RVOPF(FcvtswOp); // i32 -> f32
RVOP(FcvtwsRtzOp); // f32 -> i32, round to zero
RVOPF(FmvwxOp); // copies bit pattern from i32 to f32
RVOP(FeqOp); // Note these Ops must have been added with a `.s` in Dump.
RVOP(FltOp);
RVOP(FleOp);
RVOPF(FaddOp);
RVOPF(FsubOp);
RVOPF(FmulOp);
RVOPF(FdivOp);
RVOPF(FmvOp);

inline bool hasRd(Op *op) {
  return 
    isa<AddOp>(op) ||
    isa<AddiwOp>(op) ||
    isa<AddwOp>(op) ||
    isa<AddiOp>(op) ||
    isa<SubOp>(op) ||
    isa<SubwOp>(op) ||
    isa<MulwOp>(op) ||
    isa<MulOp>(op) ||
    isa<DivwOp>(op) ||
    isa<DivOp>(op) ||
    isa<sys::rv::LoadOp>(op) ||
    isa<LiOp>(op) ||
    isa<MvOp>(op) ||
    isa<ReadRegOp>(op) ||
    isa<SlliwOp>(op) ||
    isa<SrliwOp>(op) ||
    isa<SraiwOp>(op) ||
    isa<SraiOp>(op) ||
    isa<SeqzOp>(op) ||
    isa<SnezOp>(op) ||
    isa<SltOp>(op) ||
    isa<SltiOp>(op) ||
    isa<MulhOp>(op) ||
    isa<MulhuOp>(op) ||
    isa<LaOp>(op) ||
    isa<AndOp>(op) ||
    isa<OrOp>(op) ||
    isa<XorOp>(op) ||
    isa<AndiOp>(op) ||
    isa<OriOp>(op) ||
    isa<XoriOp>(op) ||
    isa<GetArgOp>(op) ||
    isa<FaddOp>(op) ||
    isa<FsubOp>(op) ||
    isa<FmulOp>(op) ||
    isa<FdivOp>(op) ||
    isa<FcvtswOp>(op) ||
    isa<FcvtwsRtzOp>(op) ||
    isa<FmvwxOp>(op) ||
    isa<FeqOp>(op) ||
    isa<FltOp>(op) ||
    isa<FleOp>(op) ||
    isa<FmvOp>(op);
}

}

}

#undef RVOP

#endif
