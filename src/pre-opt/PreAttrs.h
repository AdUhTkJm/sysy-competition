#ifndef PRE_ATTRS_H
#define PRE_ATTRS_H

#include "../codegen/OpBase.h"
#include <unordered_map>

#define PREOPTLINE __LINE__ + 8388608

namespace sys {

using AffineExpr = std::vector<int>;

// It only stores the coefficients. They are to be multiplied with loop induction variables.
// subscript[0] is the coefficient for the outermost loop.
// subscript.back() is a constant, hence `subscript.size()` is the loop nest depth plus 1.
class SubscriptAttr : public AttrImpl<SubscriptAttr, PREOPTLINE> {
public:
  AffineExpr subscript;
  SubscriptAttr(const AffineExpr &subscript):
    subscript(subscript) {}
  
  std::string toString() override;
  SubscriptAttr *clone() override { return new SubscriptAttr(subscript); }
};

class BaseAttr : public AttrImpl<BaseAttr, PREOPTLINE> {
public:
  Op *base;
  BaseAttr(Op *base): base(base) {}
  
  std::string toString() override;
  BaseAttr *clone() override { return new BaseAttr(base); }
};

// Shows whether the loop is parallelizable;
// For loops without loop-carried dependencies, it's trivial,
// but if there is a single dependency of a scalar, then it's considered an accumulator.
class ParallelizableAttr : public AttrImpl<ParallelizableAttr, PREOPTLINE> {
public:
  Op *accum;
  ParallelizableAttr(): accum(nullptr) {}
  ParallelizableAttr(Op *accum): accum(accum) {}

  std::string toString() override;
  ParallelizableAttr *clone() override { return new ParallelizableAttr(accum); }
};

class NoStoreAttr : public AttrImpl<NoStoreAttr, PREOPTLINE> {
public:
  std::string toString() override { return "<no store>"; }
  NoStoreAttr *clone() override { return new NoStoreAttr; }
};

}

#define SUBSCRIPT(op) (op)->get<SubscriptAttr>()->subscript
#define BASE(op) (op)->get<BaseAttr>()->base
#define PARALLEL(op) (op)->has<ParallelizableAttr>()
#define ACCUM(op) (op)->get<ParallelizableAttr>()->accum

#endif
