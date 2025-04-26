#ifndef RV_OPS_H
#define RV_OPS_H

#include "../codegen/OpBase.h"

// Don't forget that we actually rely on OpID, and __LINE__ can duplicate with codegen/Ops.h.
#define RVOP(Ty) \
  class Ty : public OpImpl<Ty, __LINE__ + 524288> { \
  public: \
    Ty(const std::vector<Value> &values): OpImpl(values) { \
      setName("rv."#Ty); \
    } \
    Ty(): OpImpl({}) { \
      setName("rv."#Ty); \
    } \
    Ty(const std::vector<Attr*> &attrs): OpImpl({}) { \
      setName("rv."#Ty); \
      this->attrs = attrs; \
    } \
    Ty(const std::vector<Value> &values, const std::vector<Attr*> &attrs): OpImpl(values) { \
      setName("rv."#Ty); \
      this->attrs = attrs; \
    } \
  }

namespace sys {

namespace rv {

// To add an op:
//    1) Check RegAlloc.cpp, the list of LOWER(...)
//    2) Check RegAlloc.cpp, the function hasRd().

RVOP(LiOp);
RVOP(AddOp);
RVOP(AddiwOp);
RVOP(AddiOp); // Note that pointers can't be `addiw`'d.
RVOP(SubOp);
RVOP(MulwOp);
RVOP(MulOp);
RVOP(DivwOp); // Signed; divu for unsigned.
RVOP(DivOp);
RVOP(SlliwOp); // Shift left.
RVOP(SrliwOp); // Shift right, unsigned.
RVOP(SraiOp); // Shift right (64 bit), signed.
RVOP(SraiwOp); // Shift right, signed.
RVOP(MulhOp); // Higher bits of mul, signed.
RVOP(MulhuOp); // Higher bits of mul, unsigned.
RVOP(BneOp);
RVOP(BeqOp);
RVOP(BltOp);
RVOP(BgeOp);
RVOP(BezOp);
RVOP(BnezOp);
RVOP(JOp);
RVOP(MvOp);
RVOP(RetOp);
RVOP(LoadOp);
RVOP(StoreOp);
RVOP(SubSpOp); // Allocate stack space: sub sp, sp, <IntAttr>
RVOP(ReadRegOp); // Read from real register
RVOP(WriteRegOp); // Write to real register
RVOP(CallOp);

}

}

#undef RVOP

#endif
