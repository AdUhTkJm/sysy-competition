#ifndef OPBASE_H
#define OPBASE_H

#include <algorithm>
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
  BasicBlock *getLastBlock() { return *--bbs.end(); }

  iterator begin() { return bbs.begin(); }
  iterator end() { return bbs.end(); }

  Op *getParent() { return parent; }

  BasicBlock *appendBlock();
  void dump(std::ostream &os, int depth);

  BasicBlock *insert(BasicBlock* at);
  BasicBlock *insertAfter(BasicBlock* at);
  void remove(BasicBlock* at);

  void insert(iterator at, BasicBlock *bb);
  void insertAfter(iterator at, BasicBlock *bb);
  void remove(iterator at);

  // Updates `preds` for every basic block inside this region. 
  void updatePreds();

  // Moves all blocks in `from` after `insertionPoint`.
  // Returns the first and final block.
  std::pair<BasicBlock*, BasicBlock*> moveTo(BasicBlock *insertionPoint);

  Region(Op *parent): parent(parent) {}
};

class BasicBlock {
  std::list<Op*> ops;
  Region *parent;
  Region::iterator place;

  friend class Region;
  friend class Op;
  
public:
  // Available for modification.
  std::set<BasicBlock*> preds;

  using iterator = decltype(ops)::iterator;

  BasicBlock(Region *parent, Region::iterator place):
    parent(parent), place(place) {}

  auto &getOps() { return ops; }
  Op *getFirstOp() { return *ops.begin(); }
  Op *getLastOp() { return *--ops.end(); }

  iterator begin() { return ops.begin(); }
  iterator end() { return ops.end(); }

  Region *getParent() { return parent; }

  // Inserts before `at`.
  void insert(iterator at, Op *op);
  void insertAfter(iterator at, Op *op);
  void remove(iterator at);

  void moveAllOpsTo(BasicBlock *bb);

  // Moves every op after `op` to `dest`, including `op`.
  void splitOpsAfter(BasicBlock *dest, Op *op);
  // Moves every op before `op` to `dest`, not including `op`.
  void splitOpsBefore(BasicBlock *dest, Op *op);

  void moveBefore(BasicBlock *bb);
  void moveAfter(BasicBlock *bb);
  void moveToEnd(Region *region);

  void erase();
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
  
  virtual ~Attr() {}
  virtual std::string toString() = 0;
};

class Op {
protected:
  const int id;

  std::set<Op*> uses;
  std::vector<Value> operands;
  std::vector<Region*> regions;
  std::vector<Attr*> attrs;
  BasicBlock *parent;
  BasicBlock::iterator place;
  Value result;

  friend class Builder;
  friend class BasicBlock;

  std::string opname;
  // This is for ease of writing macro.
  void setName(std::string name);

public:
  int getID() const { return id; }
  const std::string &getName() { return opname; }
  BasicBlock *getParent() { return parent; }
  Op *getParentOp();

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
  
  void moveBefore(Op *op);
  void moveAfter(Op *op);
  void moveToEnd(BasicBlock *block);

  template<class T>
  bool hasAttr() {
    for (auto x : attrs)
      if (isa<T>(x))
        return true;
    return false;
  }

  template<class T>
  T *getAttr() {
    for (auto x : attrs)
      if (isa<T>(x))
        return cast<T>(x);
    assert(false);
  }

  template<class T, class... Args>
  void addAttr(Args... args) {
    auto attr = new T(std::forward<Args>(args)...);
    attrs.push_back(attr);
  }

  template<class T>
  std::vector<T*> findAll() {
    std::vector<T*> result;
    if (isa<T>(this))
      result.push_back(cast<T>(this));

    for (auto region : getRegions())
      for (auto bb : region->getBlocks())
        for (auto x : bb->getOps()) {
          auto v = x->findAll<T>();
          std::copy(v.begin(), v.end(), std::back_inserter(result));
        }

    return result;
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
