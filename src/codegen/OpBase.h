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

class Value {
public:
  Op *defining;
  enum Type {
    unit, i32, i64, f32
  } ty;

  Value() {} // uninitialized, for std::map
  Value(Op *from);

  bool operator==(Value x) const { return defining == x.defining; }
  bool operator!=(Value x) const { return defining != x.defining; }
  bool operator<(Value x) const { return defining < x.defining; }
  bool operator>(Value x) const { return defining > x.defining; }
  bool operator<=(Value x) const { return defining <= x.defining; }
  bool operator>=(Value x) const { return defining >= x.defining; }
};

class Region {
  std::list<BasicBlock*> bbs;
  Op *parent;

  // For debug purposes.
  void showLiveIn();
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
  // Updates `dominators` for every basic block inside this region.
  void updateDoms();
  // Updates `liveIn` and `liveOut` for every basic block inside this region.
  void updateLiveness();

  // Moves all blocks in `from` after `insertionPoint`.
  // Returns the first and final block.
  std::pair<BasicBlock*, BasicBlock*> moveTo(BasicBlock *insertionPoint);

  void erase();

  Region(Op *parent): parent(parent) {}
};

class BasicBlock {
  std::list<Op*> ops;
  Region *parent;
  Region::iterator place;
  std::set<BasicBlock*> preds;
  std::set<BasicBlock*> succs;
  std::set<BasicBlock*> reachables;
  // Note these are dominatORs, which mean `this` is dominatED by the elements.
  std::set<BasicBlock*> doms;
  // Dominance frontiers. `this` dominatES all blocks which are preds of the elements.
  std::set<BasicBlock*> domFront;
  BasicBlock *idom = nullptr;
  // Variable (results of the ops) alive at the beginning of this block.
  std::set<Op*> liveIn;
  // Variable (results of the ops) alive at the end of this block.
  std::set<Op*> liveOut;

  friend class Region;
  friend class Op;

  // Does not check if there's any preds.
  // Used when a whole region is removed.
  void forceErase();
public:
  using iterator = decltype(ops)::iterator;

  BasicBlock(Region *parent, Region::iterator place):
    parent(parent), place(place) {}

  auto &getOps() { return ops; }
  Op *getFirstOp() { return *ops.begin(); }
  Op *getLastOp() { return *--ops.end(); }

  iterator begin() { return ops.begin(); }
  iterator end() { return ops.end(); }

  Region *getParent() { return parent; }

  const auto &getPreds() { return preds; }
  const auto &getSuccs() { return succs; }
  const auto &getDoms() { return doms; }
  const auto &getDominanceFrontier() { return domFront; }
  const auto &getLiveIn() { return liveIn; }
  const auto &getLiveOut() { return liveOut; }
  const auto &getReachables() { return reachables; }

  std::vector<Op*> getPhis();
  
  BasicBlock *getIdom() { return idom; }
  BasicBlock *nextBlock();

  bool reachable(BasicBlock *bb) { return reachables.count(bb); }
  bool dominatedBy(BasicBlock *bb) { return doms.count(bb); }
  bool dominates(BasicBlock *bb) { return bb->doms.count(this); }

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

class Attr {
  const int id;
  int refcnt = 0;

  friend class Op;
  friend class Builder;
public:
  int getID() const { return id; }
  Attr(int id): id(id) {}
  
  virtual ~Attr() {}
  virtual std::string toString() = 0;
  virtual Attr *clone() = 0;
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
  Value::Type resultTy;

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
  Op *prevOp();
  Op *nextOp();

  const auto &getUses() const { return uses; }
  const auto &getRegions() const { return regions; }
  const auto &getOperands() const { return operands; }
  const auto &getAttrs() const { return attrs; }

  Region *getRegion(int i = 0) { return regions[i]; }
  Value getOperand(int i = 0) { return operands[i]; }

  void pushOperand(Value v);
  void removeAllOperands();
  // This does a linear search, as Ops at most have 2 regions.
  void removeRegion(Region *region);

  Value getResult() { return Value(this); }
  Value::Type getResultType() const { return resultTy; }

  Op(int id, Value::Type resultTy, const std::vector<Value> &values);
  Op(int id, Value::Type resultTy, const std::vector<Value> &values, const std::vector<Attr*> &attrs);

  Region *appendRegion();
  BasicBlock *createFirstBlock();
  void erase();
  void replaceAllUsesWith(Op *other);

  void dump(std::ostream&, int depth = 0);
  
  void moveBefore(Op *op);
  void moveAfter(Op *op);
  void moveToEnd(BasicBlock *block);

  template<class T>
  bool has() {
    for (auto x : attrs)
      if (isa<T>(x))
        return true;
    return false;
  }

  template<class T>
  T *get() {
    for (auto x : attrs)
      if (isa<T>(x))
        return cast<T>(x);
    assert(false);
  }

  template<class T>
  T *find() {
    for (auto x : attrs)
      if (isa<T>(x))
        return cast<T>(x);
    return nullptr;
  }

  template<class T>
  void remove() {
    for (auto it = attrs.begin(); it != attrs.end(); it++)
      if (isa<T>(*it)) {
        (*it)->refcnt--;
        attrs.erase(it);
        return;
      }
  }

  template<class T, class... Args>
  void add(Args... args) {
    auto attr = new T(std::forward<Args>(args)...);
    attr->refcnt++;
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

  template<class T>
  T *getParentOp() {
    auto parent = getParentOp();
    while (!isa<T>(parent))
      parent = parent->getParentOp();

    return cast<T>(parent);
  }
};


template<class T, int OpID>
class OpImpl : public Op {
public:
  static bool classof(Op *op) {
    return op->getID() == OpID;
  }

  OpImpl(Value::Type resultTy, const std::vector<Value> &values): Op(OpID, resultTy, values) {}
  OpImpl(Value::Type resultTy, const std::vector<Value> &values, const std::vector<Attr*> &attrs):
    Op(OpID, resultTy, values, attrs) {}
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
