#ifndef BV_MATCHER_H
#define BV_MATCHER_H

#include "BvExpr.h"
#include <vector>
#include "../Matcher.h"

namespace smt {

using sys::Expr;
using sys::Atom;
using sys::List;

class BvRule {
  std::map<std::string_view, BvExpr*> binding;
  std::string_view text;
  std::vector<std::string> externalStrs;
  Expr *pattern;
  int loc = 0;
  bool failed = false;

  std::string_view nextToken();
  Expr *parse();

  bool matchExpr(Expr *expr, BvExpr *bvexpr);
  int evalExpr(Expr *expr);
  float evalFExpr(Expr *expr);
  BvExpr *buildExpr(Expr *expr);

  void dump(Expr *expr, std::ostream &os);
  void release(Expr *expr);
  BvExpr *rewriteRoot(BvExpr *expr);
public:
  using Binding = std::map<std::string, BvExpr*>;
  BvExprContext *ctx = nullptr;

  BvRule(const BvRule &other) = delete;

  BvRule(const char *text);
  ~BvRule();
  BvExpr *rewrite(BvExpr *expr);
  BvExpr *extract(const std::string &name);

  void dump(std::ostream &os = std::cerr);
};

}

#endif
