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

RVOP(MvOp);
RVOP(AddOp);
RVOP(SubOp);
RVOP(MulOp);
RVOP(DivOp); // Signed; divu for unsigned.

}

}

#endif
