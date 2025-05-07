#ifndef MATCHER_H
#define MATCHER_H

#include "../codegen/CodeGen.h"

namespace sys {

struct Expr {
  int id;
  Expr(int id): id(id) {}
  virtual ~Expr() {}
};

struct Atom : Expr {
  template<class T>
  static bool classof(T *t) { return t->id == 1; }

  std::string_view value;
  Atom(std::string_view value): Expr(1), value(value) {}
};

struct List : Expr {
  template<class T>
  static bool classof(T *t) { return t->id == 2; }

  // The rules are living globally anyway. No need to release.
  std::vector<Expr*> elements;
  List(): Expr(2) {}
};


class Rule {
  std::map<std::string_view, Op*> binding;
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
  Rule(const char *text);
  bool rewrite(Op *op);

  void dump(std::ostream &os);
};

}

#endif
