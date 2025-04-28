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

#define ARMOP(Ty) ARMOPBASE(Value::i32, Ty)
#define ARMOPL(Ty) ARMOPBASE(Value::i64, Ty)
#define ARMOPF(Ty) ARMOPBASE(Value::f32, Ty)

namespace sys {

namespace arm {

ARMOP(MovOp);
ARMOP(AddOp);
ARMOP(SubOp);
ARMOP(MulOp);
ARMOP(SdivOp);
ARMOP(MlaOp);


}

}

#endif
