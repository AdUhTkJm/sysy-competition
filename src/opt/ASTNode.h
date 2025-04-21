#ifndef ASTNODE_H
#define ASTNODE_H

#include <functional>
#include <string>

namespace sys {

class ASTNode;
using ASTWalker = std::function<void (ASTNode *)>;

class ASTNode {
  int id;
public:
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
