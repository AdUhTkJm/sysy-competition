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

}

#define SUBSCRIPT(op) (op)->get<SubscriptAttr>()->subscript

#endif
