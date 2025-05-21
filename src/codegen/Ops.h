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

// Ops that must be explicitly set a result type.
#define OPE(Ty) \
  class Ty : public OpImpl<Ty, __LINE__> { \
  public: \
    Ty(Value::Type resultTy, const std::vector<Value> &values): OpImpl(resultTy, values) { \
      setName(#Ty); \
    } \
    Ty(Value::Type resultTy): OpImpl(resultTy, {}) { \
      setName(#Ty); \
    } \
    Ty(Value::Type resultTy, const std::vector<Attr*> &attrs): OpImpl(resultTy, {}, attrs) { \
      setName(#Ty); \
    } \
    Ty(Value::Type resultTy, const std::vector<Value> &values, const std::vector<Attr*> &attrs): OpImpl(resultTy, values, attrs) { \
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
OP(AndIOp);
OP(OrIOp);
OP(XorIOp);
OPF(AddFOp);
OPF(SubFOp);
OPF(MulFOp);
OPF(DivFOp);
OPF(ModFOp);
OPL(AddLOp);
OPL(MulLOp);
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
OPE(GetArgOp);
OP(StoreOp); // Operand order: value, dst
OPE(LoadOp);
OP(ReturnOp);
OP(IfOp);
OP(WhileOp);
OP(ProceedOp);
OP(GotoOp);   // Jumps unconditionally.
OP(BranchOp); // Branches according to the only operand.
OP(GlobalOp);
OP(GetGlobalOp);
OP(MemcpyOp); // Operand order: dst, src
OPE(CallOp);
OP(PhiOp);
OP(F2IOp);
OPF(I2FOp);
OP(MinusOp); // for input x, returns -x. Don't confuse with SubI/SubF.
OPF(MinusFOp);
OP(NotOp);
OP(LShiftOp);
OPL(LShiftLOp);
OP(RShiftOp);
OPL(RShiftLOp);
OP(MulshOp);
OP(MuluhOp);
OP(SetNotZeroOp);
OP(BreakOp);
OP(ContinueOp);
OP(UndefOp); // Put directly under ModuleOp, not in any function.

}

#undef OP
#define DEF(i) getOperand(i).defining

#endif
