#ifndef ARM_OPS_H
#define ARM_OPS_H

#include "../codegen/OpBase.h"

#define ARMOP(Ty) \
  class Ty : public OpImpl<Ty, __LINE__ + 1048576> { \
  public: \
    Ty(const std::vector<Value> &values): OpImpl(values) { \
      setName("arm."#Ty); \
    } \
    Ty(): OpImpl({}) { \
      setName("arm."#Ty); \
    } \
    Ty(const std::vector<Attr*> &attrs): OpImpl({}) { \
      setName("arm."#Ty); \
      this->attrs = attrs; \
    } \
    Ty(const std::vector<Value> &values, const std::vector<Attr*> &attrs): OpImpl(values) { \
      setName("arm."#Ty); \
      this->attrs = attrs; \
    } \
  }

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
