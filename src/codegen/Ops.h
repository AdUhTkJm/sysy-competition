#ifndef OPS_H
#define OPS_H

#include "OpBase.h"

#define OPBASE(ValueTy, Ty) \
  class Ty : public OpImpl<Ty, __LINE__> { \
  public: \
    Ty(const std::vector<Value> &values): OpImpl(ValueTy, values) { \
      setName(#Ty); \
    } \
    Ty(): OpImpl(ValueTy, {}) { \
      setName(#Ty); \
    } \
    Ty(const std::vector<Attr*> &attrs): OpImpl(ValueTy, {}, attrs) { \
      setName(#Ty); \
    } \
    Ty(const std::vector<Value> &values, const std::vector<Attr*> &attrs): OpImpl(ValueTy, values, attrs) { \
      setName(#Ty); \
    } \
  }

#define OP(Ty) OPBASE(Value::i32, Ty)
#define OPF(Ty) OPBASE(Value::f32, Ty)
#define OPL(Ty) OPBASE(Value::i64, Ty)

namespace sys {

OP(ModuleOp);
OP(AddIOp);
OP(SubIOp);
OP(MulIOp);
OP(DivIOp);
OP(ModIOp);
OPF(AddFOp);
OPF(SubFOp);
OPF(MulFOp);
OPF(DivFOp);
OPF(ModFOp);
OPL(AddLOp);
OPL(SubLOp);
OPL(MulLOp);
OPL(DivLOp);
OP(EqOp);
OP(NeOp);
OP(LtOp);
OP(LeOp);
OP(EqFOp);
OP(NeFOp);
OP(LtFOp);
OP(LeFOp);
OP(FuncOp);
OP(IntOp);
OPF(FloatOp);
OPL(AllocaOp);
OP(GetArgOp);
OP(StoreOp); // Operand order: value, dst
OP(LoadOp);
OP(ReturnOp);
OP(IfOp);
OP(WhileOp);
OP(ProceedOp);
OP(GotoOp);   // Jumps unconditionally.
OP(BranchOp); // Branches according to the only operand.
OP(GlobalOp);
OP(GetGlobalOp);
OP(MemcpyOp); // Operand order: dst, src
OP(CallOp);
OP(PhiOp);
OP(F2IOp);
OPF(I2FOp);
OP(MinusOp); // for input x, returns -x. Don't confuse with SubI/SubF.
OPF(MinusFOp);
OP(NotOp);
OP(LShiftImmOp);
OP(RShiftImmOp);
OPL(RShiftImmLOp); // Shift for 64 bit.
OP(MulshOp);
OP(MuluhOp);

}

#undef OP

#endif
