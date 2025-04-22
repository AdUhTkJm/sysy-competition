#include "ASTNode.h"
#include <map>

using namespace sys;

#define WALK(Ty) void Ty::walk(ASTWalker walker)
#define TOSTR(Ty) std::string Ty::toString() const

WALK(IntNode) {
  walker(this);
}

TOSTR(IntNode) {
  return std::to_string(value);
}

WALK(BlockNode) {
  walker(this);
  for (auto x : nodes)
    x->walk(walker);
}

TOSTR(BlockNode) {
  return "";
}

WALK(TransparentBlockNode) {
  walker(this);
  for (auto x : nodes)
    x->walk(walker);
}

TOSTR(TransparentBlockNode) {
  return "transparent";
}

WALK(VarDeclNode) {
  walker(this);
  init->walk(walker);
}

TOSTR(VarDeclNode) {
  return "let " + name + " : " + type->toString();
}

WALK(VarRefNode) {
  walker(this);
}

TOSTR(VarRefNode) {
  return name;
}

WALK(BinaryNode) {
  walker(this);
  l->walk(walker);
  r->walk(walker);
}

TOSTR(BinaryNode) {
  static std::map<decltype(kind), std::string> mapping = {
    { Add, "+" },
    { Sub, "-" },
    { Div, "*" },
    { Mul, "/" },
    { Mod, "%" },
    { Eq, "==" },
    { Ne, "!=" },
    { Le, "<=" },
    { Lt, "<" },
  };

  return mapping[kind];
}

WALK(UnaryNode) {
  walker(this);
  node->walk(walker);
}

TOSTR(UnaryNode) {
  static std::map<decltype(kind), std::string> mapping = {
    { Minus, "-" },
    { Not, "!" },
    { Float2Int, "(int)" },
    { Int2Float, "(float)" }
  };

  return mapping[kind];
}

WALK(FnDeclNode) {
  walker(this);
  body->walk(walker);
}

TOSTR(FnDeclNode) {
  return "fn " + name;
}

WALK(ReturnNode) {
  walker(this);
}

TOSTR(ReturnNode) {
  return "return";
}

WALK(IfNode) {
  walker(this);
  cond->walk(walker);
  ifso->walk(walker);
  if (ifnot)
    ifnot->walk(walker);
}

TOSTR(IfNode) {
  return "if";
}

WALK(AssignNode) {
  walker(this);
  l->walk(walker);
  r->walk(walker);
}

TOSTR(AssignNode) {
  return "=";
}
