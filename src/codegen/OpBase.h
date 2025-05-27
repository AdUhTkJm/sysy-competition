#ifndef OPBASE_H
#define OPBASE_H

#include <algorithm>
#include <iterator>
#include <list>
#include <iostream>
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
  // Value itself shouldn't record type.
  // The type is changeable and should be carried by the (unique) op.
  enum Type {
    unit, i32, i64, f32, i128, f128
  };

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

  // Updates `preds` and `succs`. 
  void updatePreds();

  // Updates dominators. Will also update preds.
  void updateDoms();

  // Updates dominance frontier. Will also update doms.
  void updateDomFront();

  // Updates postdominators.
  void updatePDoms();

  // Updates `liveIn` and `liveOut`.
  void updateLiveness();

  // Moves all blocks in `from` after `insertionPoint`.
  // Returns the first and final block.
  std::pair<BasicBlock*, BasicBlock*> moveTo(BasicBlock *insertionPoint);

  void erase();

  Region(Op *parent): parent(parent) {}
};

class SimplifyCFG;
class BasicBlock {
  std::list<Op*> ops;
  Region *parent;
  Region::iterator place;
  // Note these are dominatORs, which mean `this` is dominatED by the elements.
  std::set<BasicBlock*> doms;
  // Dominance frontiers. `this` dominatES all blocks which are preds of the elements.
  std::set<BasicBlock*> domFront;
  BasicBlock *idom = nullptr;
  // Similarly, post dominators.
  std::set<BasicBlock*> postdoms;
  std::set<BasicBlock*> postdomFront;
  // Immediate post dominator.
  BasicBlock *ipdom = nullptr;
  // Variable (results of the ops) alive at the beginning of this block.
  std::set<Op*> liveIn;
  // Variable (results of the ops) alive at the end of this block.
  std::set<Op*> liveOut;

  friend class Region;
  friend class Op;
public:
  std::set<BasicBlock*> preds;
  std::set<BasicBlock*> succs;
  using iterator = decltype(ops)::iterator;

  BasicBlock(Region *parent, Region::iterator place):
    parent(parent), place(place) {}

  auto &getOps() { return ops; }
  Op *getFirstOp() const { return *ops.begin(); }
  Op *getLastOp() const { return *--ops.end(); }

  iterator begin() { return ops.begin(); }
  iterator end() { return ops.end(); }

  Region *getParent() const { return parent; }

  const auto &getDominanceFrontier() const { return domFront; }
  const auto &getPDoms() const { return postdoms; }
  const auto &getPDomFrontier() const { return postdomFront; }
  const auto &getLiveIn() const { return liveIn; }
  const auto &getLiveOut() const { return liveOut; }

  std::vector<Op*> getPhis() const;
  
  BasicBlock *getIdom() const { return idom; }
  BasicBlock *getIPdom() const { return ipdom; }
  BasicBlock *nextBlock() const;

  bool dominatedBy(const BasicBlock *bb) const;
  bool dominates(const BasicBlock *bb) const { return bb->dominatedBy(this); }
  bool postDominatedBy(const BasicBlock *bb) const { return postdoms.count(const_cast<BasicBlock*>(bb)); }
  bool postDominates(const BasicBlock *bb) const { return bb->postdoms.count(const_cast<BasicBlock*>(bb)); }

  // Inserts before `at`.
  void insert(iterator at, Op *op);
  void insertAfter(iterator at, Op *op);
  void remove(iterator at);

  // Moves to end by default.
  void moveAllOpsTo(BasicBlock *bb);

  // Moves every op after `op` to `dest`, including `op`.
  void splitOpsAfter(BasicBlock *dest, Op *op);
  // Moves every op before `op` to `dest`, not including `op`.
  void splitOpsBefore(BasicBlock *dest, Op *op);

  void moveBefore(BasicBlock *bb);
  void moveAfter(BasicBlock *bb);
  void moveToEnd(Region *region);

  void erase();

  // Does not check if there's any preds.
  // Used when a lot of blocks are going to get removed.
  void forceErase();
};

class Attr {
  int refcnt = 0;

  friend class Op;
  friend class Builder;
public:
  const int attrid;
  Attr(int id): attrid(id) {}
  
  virtual ~Attr() {}
  virtual std::string toString() = 0;
  virtual Attr *clone() = 0;
};

class Op {
protected:
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
  void removeOperandUse(Op *op);

  static std::vector<Op*> toDelete;
public:
  const int opid;

  const std::string &getName() { return opname; }
  BasicBlock *getParent() { return parent; }
  Op *getParentOp();
  Op *prevOp();
  Op *nextOp();

  const auto &getUses() const { return uses; }
  const auto &getRegions() const { return regions; }
  const auto &getOperands() const { return operands; }
  const auto &getAttrs() const { return attrs; }

  int getOperandCount() const { return operands.size(); }
  int getRegionCount() const { return regions.size(); }

  Region *getRegion(int i = 0) { return regions[i]; }
  Value getOperand(int i = 0) { return operands[i]; }

  void pushOperand(Value v);
  void removeAllOperands();
  void setOperand(int i, Value v);
  void removeOperand(int i);
  int replaceOperand(Op *before, Value v);

  void removeAllAttributes();
  void removeAttribute(int i);
  void setAttribute(int i, Attr *attr);

  // This does a linear search, as Ops at most have 2 regions.
  void removeRegion(Region *region);

  Value getResult() { return Value(this); }
  Value::Type getResultType() const { return resultTy; }
  void setResultType(Value::Type ty) { resultTy = ty; }

  Op(int id, Value::Type resultTy, const std::vector<Value> &values);
  Op(int id, Value::Type resultTy, const std::vector<Value> &values, const std::vector<Attr*> &attrs);

  Region *appendRegion();
  BasicBlock *createFirstBlock();
  void erase();
  void replaceAllUsesWith(Op *other);

  void dump(std::ostream& = std::cerr, int depth = 0);
  
  void moveBefore(Op *op);
  void moveAfter(Op *op);
  void moveToEnd(BasicBlock *block);
  void moveToStart(BasicBlock *block);

  bool inside(Op *op);
  bool atFront() { return this == parent->getFirstOp(); }
  bool atBack() { return this == parent->getLastOp(); }

  // erase() will delay its deletion.
  // This function must be called to actually call `operator delete`.
  static void release();

  static Op *getPhiFrom(Op *phi, BasicBlock *bb);
  static BasicBlock *getPhiFrom(Op *phi, Op *op);

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
        if (!--(*it)->refcnt)
          delete *it;
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
  std::vector<Op*> findAll() {
    std::vector<Op*> result;
    if (isa<T>(this))
      result.push_back(this);

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

inline std::ostream &operator<<(std::ostream &os, Op *op) {
  op->dump(os);
  return os;
}

template<class T, int OpID>
class OpImpl : public Op {
public:
  constexpr static int id = OpID;
  
  static bool classof(Op *op) {
    return op->opid == OpID;
  }

  OpImpl(Value::Type resultTy, const std::vector<Value> &values): Op(OpID, resultTy, values) {}
  OpImpl(Value::Type resultTy, const std::vector<Value> &values, const std::vector<Attr*> &attrs):
    Op(OpID, resultTy, values, attrs) {}
};

template<class T, int AttrID>
class AttrImpl : public Attr {
public:
  static bool classof(Attr *attr) {
    return attr->attrid == AttrID;
  }

  AttrImpl(): Attr(AttrID) {}
};

};

#endif
