#ifndef OPS_H
#define OPS_H

#include "OpBase.h"

#define OP(Ty) \
  class Ty : public OpImpl<Ty, __LINE__> { \
  public: \
    Ty(const std::vector<Value> &values): OpImpl(values) { \
      setName(#Ty); \
    } \
    Ty(): OpImpl({}) { \
      setName(#Ty); \
    } \
    Ty(const std::vector<Attr*> &attrs): OpImpl({}) { \
      setName(#Ty); \
      this->attrs = attrs; \
    } \
  }

namespace sys {

OP(ModuleOp);
OP(AddIOp);
OP(SubIOp);
OP(MulIOp);
OP(DivIOp);
OP(ModIOp);
OP(EqOp);
OP(NeOp);
OP(LtOp);
OP(LeOp);
OP(FuncOp);
OP(IntOp);
OP(AllocaOp);
OP(GetArgOp);
OP(StoreOp);
OP(LoadOp);
OP(ReturnOp);
OP(IfOp);
OP(WhileOp);
OP(ProceedOp);

}

#endif
