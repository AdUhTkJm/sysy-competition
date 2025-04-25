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

WALK(FloatNode) {
  walker(this);
}

TOSTR(FloatNode) {
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

WALK(WhileNode) {
  walker(this);
  cond->walk(walker);
  body->walk(walker);
}

TOSTR(WhileNode) {
  return "while";
}

WALK(ConstArrayNode) {
  walker(this);
}

TOSTR(ConstArrayNode) {
  return "const-array";
}

WALK(LocalArrayNode) {
  walker(this);
}

TOSTR(LocalArrayNode) {
  return "local-array";
}

WALK(ArrayAccessNode) {
  walker(this);
  array->walk(walker);
  index->walk(walker);
}

TOSTR(ArrayAccessNode) {
  return "[]";
}

WALK(ArrayAssignNode) {
  walker(this);
  array->walk(walker);
  for (auto x : indices)
    x->walk(walker);
  value->walk(walker);
}

TOSTR(ArrayAssignNode) {
  return "[]=";
}

WALK(CallNode) {
  walker(this);
  for (auto x : args)
    x->walk(walker);
}

TOSTR(CallNode) {
  return "call " + func;
}
