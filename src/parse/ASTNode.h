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
  int id;
public:
  Type *type;

  int getID() const { return id; }

  virtual void walk(ASTWalker walker) = 0;

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
  
  void walk(ASTWalker walker) {
    walker(this);
  }
};

class BlockNode : public ASTNodeImpl<BlockNode, __LINE__> {
public:
  std::vector<ASTNode*> nodes;

  BlockNode(const decltype(nodes) &n): nodes(n) {}

  void walk(ASTWalker walker) {
    walker(this);
    for (auto x : nodes)
      x->walk(walker);
  }
};

class ConstDeclNode : public ASTNodeImpl<ConstDeclNode, __LINE__> {
public:
  std::string name;
  ASTNode *init;

  ConstDeclNode(std::string name, ASTNode *init): name(name), init(init) {}
};

class DeclNode : public ASTNodeImpl<DeclNode, __LINE__> {
public:
  std::string name;
  ASTNode *init;

  DeclNode(std::string name, ASTNode *init): name(name), init(init) {}
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
};

class UnaryNode : public ASTNodeImpl<UnaryNode, __LINE__> {
public:
  enum {
    Not, Minus,
  } kind;

  ASTNode *node;

  UnaryNode(decltype(kind) k, ASTNode *node):
    kind(k), node(node) {}
};
  
template<class T>
bool isa(T *t) {
  return T::classof(t);
}

template<class T>
T *cast(T *t) {
  assert(isa<T>(t));
  return (T*) t;
}

template<class T>
T *dyn_cast(T *t) {
  if (!isa<T>(t))
    return nullptr;
  return cast<T>(t);
}

};

#endif
