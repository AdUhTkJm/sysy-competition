#ifndef ATTRS_H
#define ATTRS_H

#include "OpBase.h"
#include <climits>
#include <cstdint>
#include <map>

namespace sys {

class NameAttr : public AttrImpl<NameAttr, __LINE__> {
public:
  std::string name;

  NameAttr(std::string name): name(name) {}

  std::string toString() { return "<name = " + name + ">"; }
  NameAttr *clone() { return new NameAttr(name); }
};

class IntAttr : public AttrImpl<IntAttr, __LINE__> {
public:
  int value;

  IntAttr(int value): value(value) {}

  std::string toString() { return "<" + std::to_string(value) + ">"; }
  IntAttr *clone() { return new IntAttr(value); }
};

class FloatAttr : public AttrImpl<FloatAttr, __LINE__> {
public:
  float value;

  FloatAttr(float value): value(value) {}

  std::string toString() { return "<" + std::to_string(value) + "f>"; }
  FloatAttr *clone() { return new FloatAttr(value); }
};

class SizeAttr : public AttrImpl<SizeAttr, __LINE__> {
public:
  size_t value;

  SizeAttr(size_t value): value(value) {}

  std::string toString() { return "<size = " + std::to_string(value) + ">"; }
  SizeAttr *clone() { return new SizeAttr(value); }
};

// A map for printing purposes.
extern std::map<BasicBlock*, int> bbmap;
extern int bbid;

// The target for GotoOp, and for BranchOp if the condition is true.
class TargetAttr : public AttrImpl<TargetAttr, __LINE__> {
public:
  BasicBlock *bb;

  TargetAttr(BasicBlock *bb): bb(bb) {}

  std::string toString();
  TargetAttr *clone() { return new TargetAttr(bb); }
};

// The target for BranchOp if the condition is false.
class ElseAttr : public AttrImpl<ElseAttr, __LINE__> {
public:
  BasicBlock *bb;

  ElseAttr(BasicBlock *bb): bb(bb) {}

  std::string toString();
  ElseAttr *clone() { return new ElseAttr(bb); }
};

class FromAttr : public AttrImpl<FromAttr, __LINE__> {
public:
  BasicBlock *bb;

  FromAttr(BasicBlock *bb): bb(bb) {}

  std::string toString();
  FromAttr *clone() { return new FromAttr(bb); }
};

class IntArrayAttr : public AttrImpl<IntArrayAttr, __LINE__> {
public:
  int *vi;
  // This is the number of elements in `vi`, rather than byte size,
  // For example, if vi = { 2, 3 }, then `size` is 2, rather than sizeof(int) * 2.
  int size;
  bool allZero;

  IntArrayAttr(int *vi, int size);

  std::string toString();
  IntArrayAttr *clone() { return new IntArrayAttr(vi, size); }
};

class FloatArrayAttr : public AttrImpl<FloatArrayAttr, __LINE__> {
public:
  float *vf;
  // This is the number of elements in `vi`, rather than byte size,
  // For example, if vi = { 2, 3 }, then `size` is 2, rather than sizeof(int) * 2.
  int size;
  bool allZero;

  FloatArrayAttr(float *vf, int size);

  std::string toString();
  FloatArrayAttr *clone() { return new FloatArrayAttr(vf, size); }
};

class ImpureAttr : public AttrImpl<ImpureAttr, __LINE__> {
public:
  std::string toString() { return "<impure>"; }
  ImpureAttr *clone() { return new ImpureAttr; }
};

class AtMostOnceAttr : public AttrImpl<AtMostOnceAttr, __LINE__> {
public:
  std::string toString() { return "<once>"; }
  AtMostOnceAttr *clone() { return new AtMostOnceAttr; }
};

class ArgCountAttr : public AttrImpl<ArgCountAttr, __LINE__> {
public:
  int count;

  ArgCountAttr(int count): count(count) {}

  std::string toString() { return "<count = " + std::to_string(count) + ">"; }
  ArgCountAttr *clone() { return new ArgCountAttr(count); }
};

class CallerAttr : public AttrImpl<CallerAttr, __LINE__> {
public:
  // The functions in `callers` actually calls the function with this attribute.
  // For example,
  //    func <name = f> <caller = g, h>
  // means `f` is called by `g` and `h`.
  std::vector<std::string> callers;

  CallerAttr(const std::vector<std::string> &callers): callers(callers) {}
  CallerAttr() {}

  std::string toString();
  CallerAttr *clone() { return new CallerAttr(callers); }
};

class AliasAttr : public AttrImpl<AliasAttr, __LINE__> {
public:
  // All possible bases and offsets.
  // For most variables, the vectors contain only 1 element;
  // For function arguments and their strides, they might contain more.
  //
  // Semantics:
  //    location[base] = offset;
  //
  // Here `base` is either an alloca or a global. 
  // It's the GlobalOp rather than the GetGlobalOp, for deduplication.
  //
  // `offset` is an integer. When it's non-negative, it represents a known, constant offset;
  // when it's negative, it means an unknown offset. 
  // The language prohibits negative offset in source code, so this is safe.
  std::map<Op*, std::vector<int>> location;
  bool unknown;

  AliasAttr(): unknown(true) {}
  AliasAttr(Op *base, int offset): location({ { base, { offset } } }), unknown(false) {}
  AliasAttr(const decltype(location) &location): location(location), unknown(false) {}

  // Returns true if changed.
  bool add(Op *base, int offset);
  // Returns true if changed.
  bool addAll(const AliasAttr *other);
  bool mustAlias(const AliasAttr *other) const;
  bool neverAlias(const AliasAttr *other) const;
  bool mayAlias(const AliasAttr *other) const;
  std::string toString();
  AliasAttr *clone() { return unknown ? new AliasAttr() : new AliasAttr(location); }
};

class RangeAttr : public AttrImpl<RangeAttr, __LINE__> {
public:
  // Semantics:
  //    auto [low, high] = range;
  // The integer operation to which this Attr attach is in range [low, high] (closed interval).
  std::pair<int, int> range;

  RangeAttr(): range({ INT_MIN, INT_MAX }) {}
  RangeAttr(int low, int high): range({ low, high }) {}
  RangeAttr(std::pair<int, int> range): range(range) {}

  std::string toString();
  RangeAttr *clone() { return new RangeAttr(range); }
};

// Marks whether an alloca is floating point.
// This can't be deduced by return value because it's always i64.
class FPAttr : public AttrImpl<FPAttr, __LINE__> {
public:
  FPAttr() {}

  std::string toString() { return "<fp>"; }
  FPAttr *clone() { return new FPAttr; }
};

}

#define V(op) (op)->get<IntAttr>()->value
#define F(op) (op)->get<FloatAttr>()->value
#define SIZE(op) (op)->get<SizeAttr>()->value
#define NAME(op) (op)->get<NameAttr>()->name
#define TARGET(op) (op)->get<TargetAttr>()->bb
#define ELSE(op) (op)->get<ElseAttr>()->bb
#define CALLER(op) (op)->get<CallerAttr>()->callers
#define ALIAS(op) (op)->get<AliasAttr>()
#define RANGE(op) (op)->get<RangeAttr>()->range
#define FROM(attr) cast<FromAttr>(attr)->bb

#endif
