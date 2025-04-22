#ifndef OPBASE_H
#define OPBASE_H

#include <initializer_list>
#include <list>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <vector>

namespace sys {

class Op;
class BasicBlock {
  std::list<std::unique_ptr<Op>> ops;
public:
  auto &getOps() { return ops; }
};

class Region {
  std::list<std::unique_ptr<BasicBlock>> bbs;
public:
  auto &getBasicBlocks() { return bbs; }

  void appendBlock();
  void dump(std::ostream &os, int depth);
};

class Value {
  static int id;
public:
  const int name;
  Op *const defining;

  Value(Op *from): name(id++), defining(from) {}
};

class Op {
  // Note that this doesn't own Op*'s referred to here.
  std::vector<Op*> uses;
  std::vector<Value> operands;
  std::vector<std::unique_ptr<Region>> regions;
  Value result;
  BasicBlock *bb;
  
  int id;
  std::string opname;
public:
  int getID() const { return id; }

  const auto &getUses() const { return uses; }
  const auto &getRegions() const { return regions; }
  const auto &getOperands() const { return operands; }
  Value getResult() const { return result; }
  operator Value() const { return result; }

  Op(BasicBlock *bb): result(this), bb(bb) {}
  Op(BasicBlock *bb, Value value): result(this) {
    operands.push_back(value);
    value.defining->uses.push_back(this);
  }

  Op(BasicBlock *bb, std::initializer_list<Value> values): result(this) {
    for (auto x : values) {
      operands.push_back(x);
      x.defining->uses.push_back(this);
    }
  }

  void appendRegion();
  void createFirstBlock();
  void erase();

  void dump(std::ostream&, int depth = 0);
};


template<class T, int OpID>
class OpImpl : public Op {
public:
  static bool classof(Op *op) {
    return op->getID() == OpID;
  }
};

};

#endif
