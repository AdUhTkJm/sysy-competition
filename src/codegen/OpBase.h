#ifndef OPBASE_H
#define OPBASE_H

#include <iterator>
#include <list>
#include <ostream>
#include <set>
#include <string>
#include <vector>

#include "../utils/DynamicCast.h"

namespace sys {

class Op;
class BasicBlock;

class Region {
  std::list<BasicBlock*> bbs;
  Op *parent;
public:
  using iterator = decltype(bbs)::iterator;

  auto &getBlocks() { return bbs; }
  BasicBlock *getFirstBlock() { return *bbs.begin(); }
  BasicBlock *getLastBlock() { return *bbs.end(); }

  iterator begin() { return bbs.begin(); }
  iterator end() { return bbs.end(); }

  Op *getParent() { return parent; }

  BasicBlock *appendBlock();
  void dump(std::ostream &os, int depth);

  Region(Op *parent): parent(parent) {}
};

class BasicBlock {
  std::list<Op*> ops;
  Region *parent;
  Region::iterator place; // The iterator *after* this basic block.
  
public:
  using iterator = decltype(ops)::iterator;

  BasicBlock(Region *parent, Region::iterator place):
    parent(parent), place(place) {}

  auto &getOps() { return ops; }
  Op *getFirstOp() { return *ops.begin(); }
  Op *getLastOp() { return *ops.end(); }

  iterator begin() { return ops.begin(); }
  iterator end() { return ops.end(); }

  Region *getParent() { return parent; }

  // Inserts before `at`.
  void insert(iterator at, Op *op) {
    ops.insert(at, op);
  }

  void insertAfter(iterator at, Op *op) {
    if (at == ops.end()) {
      ops.push_back(op);
      return;
    }
    ops.insert(++at, op);
  }
};

class Value {
public:
  Op *defining;

  Value() {} // uninitialized, for std::map
  Value(Op *from): defining(from) {}
};

class Attr {
  const int id;
public:
  int getID() const { return id; }
  Attr(int id): id(id) {}
  
  virtual std::string toString() = 0;
};

class Op {
protected:
  const int id;

  std::vector<Op*> uses;
  std::vector<Value> operands;
  std::vector<Region*> regions;
  std::vector<Attr*> attrs;
  BasicBlock *parent;
  BasicBlock::iterator place; // The iterator *after* this op.
  Value result;

  friend class Builder;

  std::string opname;
  // This is for ease of writing macro.
  void setName(std::string name);

public:
  int getID() const { return id; }
  const std::string &getName() { return opname; }

  const auto &getUses() const { return uses; }
  const auto &getRegions() const { return regions; }
  const auto &getOperands() const { return operands; }
  const auto &getAttrs() const { return attrs; }

  Region *getRegion(int i = 0) { return regions[i]; }
  Value getOperand(int i = 0) { return operands[i]; }

  Value getResult() const { return result; }
  operator Value() const { return result; }

  Op(int id, const std::vector<Value> &values);

  Region *appendRegion();
  BasicBlock *createFirstBlock();
  void erase();
  void replaceAllUsesWith(Op *other);

  void dump(std::ostream&, int depth = 0);

  template<class T>
  bool has(T attr) {
    for (auto x : attrs)
      if (isa<T>(x))
        return true;
    return false;
  }

  template<class T, class... Args>
  void addAttr(Args... args) {
    auto attr = new T(std::forward<Args>(args)...);
    attrs.push_back(attr);
  }
};


template<class T, int OpID>
class OpImpl : public Op {
public:
  static bool classof(Op *op) {
    return op->getID() == OpID;
  }

  OpImpl(const std::vector<Value> &values): Op(OpID, values) {}
};

template<class T, int AttrID>
class AttrImpl : public Attr {
public:
  static bool classof(Attr *attr) {
    return attr->getID() == AttrID;
  }

  AttrImpl(): Attr(AttrID) {}
};

};

#endif
