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

  virtual void walk(ASTWalker walker) = 0;
  virtual std::string toString() const = 0;

  ASTNode(int id): id(id) {}
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
  
  void walk(ASTWalker walker);
  std::string toString() const;
};

class FloatNode : public ASTNodeImpl<FloatNode, __LINE__> {
public:
  float value;

  FloatNode(float value): value(value) {}
  
  void walk(ASTWalker walker);
  std::string toString() const;
};

class BlockNode : public ASTNodeImpl<BlockNode, __LINE__> {
public:
  std::vector<ASTNode*> nodes;

  BlockNode(const decltype(nodes) &n): nodes(n) {}

  void walk(ASTWalker walker);
  std::string toString() const;
};

class VarDeclNode : public ASTNodeImpl<VarDeclNode, __LINE__> {
public:
  std::string name;
  ASTNode *init;
  bool mut;
  bool global;

  VarDeclNode(const std::string &name, ASTNode *init, bool mut = true, bool global = false):
    name(name), init(init), mut(mut), global(global) {}
  
  void walk(ASTWalker walker);
  std::string toString() const;
};

class VarRefNode : public ASTNodeImpl<VarRefNode, __LINE__> {
public:
  std::string name;

  VarRefNode(const std::string &name): name(name) {}

  void walk(ASTWalker walker);
  std::string toString() const;
};

// Note that we allow defining multiple variables in a single statement,
// so we have to group multiple VarDeclNodes together.
// Using a BlockNode creates a new scope, but this one does not.
class TransparentBlockNode : public ASTNodeImpl<TransparentBlockNode, __LINE__> {
public:
  std::vector<VarDeclNode*> nodes;

  TransparentBlockNode(const decltype(nodes) &n): nodes(n) {}

  void walk(ASTWalker walker);
  std::string toString() const;
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
  
  void walk(ASTWalker walker);
  std::string toString() const;
};

class UnaryNode : public ASTNodeImpl<UnaryNode, __LINE__> {
public:
  enum {
    Not, Minus, Float2Int, Int2Float
  } kind;

  ASTNode *node;

  UnaryNode(decltype(kind) k, ASTNode *node):
    kind(k), node(node) {}
  
  void walk(ASTWalker walker);
  std::string toString() const;
};

class FnDeclNode : public ASTNodeImpl<FnDeclNode, __LINE__> {
public:
  std::string name;
  std::vector<std::string> args;
  BlockNode *body;

  FnDeclNode(std::string name, const decltype(args) &a, BlockNode *body):
    name(name), args(a), body(body) {}

  void walk(ASTWalker walker);
  std::string toString() const;
};

class ReturnNode : public ASTNodeImpl<ReturnNode, __LINE__> {
public:
  ASTNode *node;

  ReturnNode(ASTNode *node): node(node) {}

  void walk(ASTWalker walker);
  std::string toString() const;
};

class IfNode : public ASTNodeImpl<IfNode, __LINE__> {
public:
  ASTNode *cond, *ifso, *ifnot;

  IfNode(ASTNode *cond, ASTNode *ifso, ASTNode *ifnot):
    cond(cond), ifso(ifso), ifnot(ifnot) {}

  void walk(ASTWalker walker);
  std::string toString() const;
};

class AssignNode : public ASTNodeImpl<AssignNode, __LINE__> {
public:
  ASTNode *l, *r;

  AssignNode(ASTNode *l, ASTNode *r): l(l), r(r) {}

  void walk(ASTWalker walker);
  std::string toString() const;
};

class WhileNode : public ASTNodeImpl<WhileNode, __LINE__> {
public:
  ASTNode *cond, *body;

  WhileNode(ASTNode *cond, ASTNode *body): cond(cond), body(body) {}

  void walk(ASTWalker walker);
  std::string toString() const;
};

// Size is to be deduced by type.
class ConstArrayNode : public ASTNodeImpl<ConstArrayNode, __LINE__> {
public:
  union {
    int *vi;
    float *vf;
  };

  ConstArrayNode(int *vi): vi(vi) {}
  ConstArrayNode(float *vf): vf(vf) {}

  void walk(ASTWalker walker);
  std::string toString() const;
};

class LocalArrayNode : public ASTNodeImpl<LocalArrayNode, __LINE__> {
public:
  ASTNode **va;

  LocalArrayNode(ASTNode **va): va(va) {}
  
  void walk(ASTWalker walker);
  std::string toString() const;
};

class ArrayAccessNode : public ASTNodeImpl<ArrayAccessNode, __LINE__> {
public:
  ASTNode *array;
  ASTNode *index;

  ArrayAccessNode(ASTNode *array, ASTNode *index):
    array(array), index(index) {}
  
  void walk(ASTWalker walker);
  std::string toString() const;
};

class CallNode : public ASTNodeImpl<CallNode, __LINE__> {
public:
  std::string func;
  std::vector<ASTNode*> args;

  CallNode(const std::string &func, const std::vector<ASTNode*> &args):
    func(func), args(args) {}

  void walk(ASTWalker walker);
  std::string toString() const;
};

};

#endif
