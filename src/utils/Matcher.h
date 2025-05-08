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
  std::vector<std::string> externalStrs;
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
  void release(Expr *expr);
public:
  using Binding = std::map<std::string, Op*>;

  Rule(const Rule &other) = delete;

  Rule(const char *text);
  ~Rule();
  bool rewrite(Op *op);
  // The caller should ensure the strings live long enough to cover the `extract`, if it will be called.
  bool match(Op *op, const Binding &external = {});
  Op *extract(const std::string &name);

  void dump(std::ostream &os);
};

}

#endif
