#ifndef ASTNODE_H
#define ASTNODE_H

#include <functional>
#include <string>
#include <vector>

#include "Type.h"

namespace sys {

class ASTNode;
using ASTWalker = std::function<void (ASTNode *)>;

class ASTNode {
  const int id;
public:
  Type *type = nullptr;

  int getID() const { return id; }

  ASTNode(int id): id(id) {}
  virtual ~ASTNode() {}
};

template<class T, int NodeID>
class ASTNodeImpl : public ASTNode {
public:
  static bool classof(ASTNode *node) {
    return node->getID() == NodeID;
  }

  ASTNodeImpl(): ASTNode(NodeID) {}
};

class IntNode : public ASTNodeImpl<IntNode, __LINE__> {
public:
  int value;

  IntNode(int value): value(value) {}
};

class FloatNode : public ASTNodeImpl<FloatNode, __LINE__> {
public:
  float value;

  FloatNode(float value): value(value) {}
};

class BlockNode : public ASTNodeImpl<BlockNode, __LINE__> {
public:
  std::vector<ASTNode*> nodes;

  BlockNode(const decltype(nodes) &n): nodes(n) {}
  ~BlockNode() { for (auto node : nodes) delete node; }
};

class VarDeclNode : public ASTNodeImpl<VarDeclNode, __LINE__> {
public:
  std::string name;
  ASTNode *init;
  bool mut;
  bool global;

  VarDeclNode(const std::string &name, ASTNode *init, bool mut = true, bool global = false):
    name(name), init(init), mut(mut), global(global) {}
  ~VarDeclNode() { delete init; }
};

class VarRefNode : public ASTNodeImpl<VarRefNode, __LINE__> {
public:
  std::string name;

  VarRefNode(const std::string &name): name(name) {}
};

// Note that we allow defining multiple variables in a single statement,
// so we have to group multiple VarDeclNodes together.
// Using a BlockNode creates a new scope, but this one does not.
class TransparentBlockNode : public ASTNodeImpl<TransparentBlockNode, __LINE__> {
public:
  std::vector<VarDeclNode*> nodes;

  TransparentBlockNode(const decltype(nodes) &n): nodes(n) {}
};

class BinaryNode : public ASTNodeImpl<BinaryNode, __LINE__> {
public:
  enum {
    Add, Sub, Mul, Div, Mod, And, Or,
    // >= and > Canonicalized.
    Eq, Ne, Le, Lt
  } kind;

  ASTNode *l, *r;

  BinaryNode(decltype(kind) k, ASTNode *l, ASTNode *r):
    kind(k), l(l), r(r) {}
  ~BinaryNode() { delete l; delete r; }
};

class UnaryNode : public ASTNodeImpl<UnaryNode, __LINE__> {
public:
  enum {
    Not, Minus, Float2Int, Int2Float
  } kind;

  ASTNode *node;

  UnaryNode(decltype(kind) k, ASTNode *node):
    kind(k), node(node) {}
  ~UnaryNode() { delete node; }
};

class FnDeclNode : public ASTNodeImpl<FnDeclNode, __LINE__> {
public:
  std::string name;
  std::vector<std::string> args;
  BlockNode *body;

  FnDeclNode(std::string name, const decltype(args) &a, BlockNode *body):
    name(name), args(a), body(body) {}
  ~FnDeclNode() { delete body; }
};

class ReturnNode : public ASTNodeImpl<ReturnNode, __LINE__> {
public:
  ASTNode *node;
  std::string func;

  ReturnNode(const std::string &func, ASTNode *node): node(node), func(func) {}
  ~ReturnNode() { delete node; }
};

class IfNode : public ASTNodeImpl<IfNode, __LINE__> {
public:
  ASTNode *cond, *ifso, *ifnot;

  IfNode(ASTNode *cond, ASTNode *ifso, ASTNode *ifnot):
    cond(cond), ifso(ifso), ifnot(ifnot) {}
  ~IfNode() { delete cond; delete ifso; delete ifnot; }
};

class AssignNode : public ASTNodeImpl<AssignNode, __LINE__> {
public:
  ASTNode *l, *r;

  AssignNode(ASTNode *l, ASTNode *r): l(l), r(r) {}
  ~AssignNode() { delete l; delete r; }
};

class WhileNode : public ASTNodeImpl<WhileNode, __LINE__> {
public:
  ASTNode *cond, *body;

  WhileNode(ASTNode *cond, ASTNode *body): cond(cond), body(body) {}
  ~WhileNode() { delete cond; delete body; }
};

// Size is to be deduced by type.
class ConstArrayNode : public ASTNodeImpl<ConstArrayNode, __LINE__> {
public:
  union {
    int *vi;
    float *vf;
  };
  bool isFloat;

  ConstArrayNode(int *vi): vi(vi), isFloat(false) {}
  ConstArrayNode(float *vf): vf(vf), isFloat(true) {}
};

class LocalArrayNode : public ASTNodeImpl<LocalArrayNode, __LINE__> {
public:
  ASTNode **va;

  LocalArrayNode(ASTNode **va): va(va) {}
};

class ArrayAccessNode : public ASTNodeImpl<ArrayAccessNode, __LINE__> {
public:
  std::string array;
  std::vector<ASTNode*> indices;
  Type *arrTy = nullptr; // Filled in Sema.

  ArrayAccessNode(const std::string &array, const std::vector<ASTNode*> &indices):
    array(array), indices(indices) {}
};

class ArrayAssignNode : public ASTNodeImpl<ArrayAssignNode, __LINE__> {
public:
  std::string array;
  std::vector<ASTNode*> indices;
  ASTNode *value;
  Type *arrTy = nullptr; // Filled in Sema.

  ArrayAssignNode(const std::string &array, const std::vector<ASTNode*> &indices, ASTNode *value):
    array(array), indices(indices), value(value) {}
};

class CallNode : public ASTNodeImpl<CallNode, __LINE__> {
public:
  std::string func;
  std::vector<ASTNode*> args;

  CallNode(const std::string &func, const std::vector<ASTNode*> &args):
    func(func), args(args) {}
};

class BreakNode : public ASTNodeImpl<BreakNode, __LINE__> {};
class ContinueNode : public ASTNodeImpl<ContinueNode, __LINE__> {};
class EmptyNode : public ASTNodeImpl<EmptyNode, __LINE__> {};

};

#endif
