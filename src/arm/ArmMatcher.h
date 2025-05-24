#ifndef ARM_MATCHER_H
#define ARM_MATCHER_H

#include "../utils/Matcher.h"

namespace sys {

// The difference is that opcode is interpreted differently.
class ArmRule {
  std::map<std::string_view, Op*> binding;
  std::map<std::string_view, BasicBlock*> blockBinding;
  std::map<std::string_view, int> imms;

  std::string_view text;
  Expr *pattern;
  Builder builder;
  int loc = 0;
  bool failed = false;

  std::string_view nextToken();
  Expr *parse();

  bool matchExpr(Expr *expr, Op *op);
  int evalExpr(Expr *expr);
  Op *buildExpr(Expr *expr);

  void dump(Expr *expr, std::ostream &os);
public:
  ArmRule(const char *text);
  bool rewrite(Op *op);

  void dump(std::ostream &os);
};

}

#endif
