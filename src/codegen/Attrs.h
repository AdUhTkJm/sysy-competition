#ifndef ATTRS_H
#define ATTRS_H

#include "OpBase.h"
#include <map>

namespace sys {

class NameAttr : public AttrImpl<NameAttr, __LINE__> {
public:
  std::string name;

  NameAttr(std::string name): name(name) {}

  std::string toString() { return "<name = " + name + ">"; }
};

class IntAttr : public AttrImpl<IntAttr, __LINE__> {
public:
  int value;

  IntAttr(int value): value(value) {}

  std::string toString() { return "<" + std::to_string(value) + ">"; }
};

class FloatAttr : public AttrImpl<FloatAttr, __LINE__> {
public:
  float value;

  FloatAttr(float value): value(value) {}

  std::string toString() { return "<" + std::to_string(value) + "f>"; }
};

class SizeAttr : public AttrImpl<SizeAttr, __LINE__> {
public:
  size_t value;

  SizeAttr(size_t value): value(value) {}

  std::string toString() { return "<size = " + std::to_string(value) + ">"; }
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
};

// The target for BranchOp if the condition is false.
class ElseAttr : public AttrImpl<ElseAttr, __LINE__> {
public:
  BasicBlock *bb;

  ElseAttr(BasicBlock *bb): bb(bb) {}

  std::string toString();
};

class FromAttr : public AttrImpl<FromAttr, __LINE__> {
public:
  BasicBlock *bb;

  FromAttr(BasicBlock *bb): bb(bb) {}

  std::string toString();
};

class IntArrayAttr : public AttrImpl<IntArrayAttr, __LINE__> {
public:
  int *vi;
  int size;

  IntArrayAttr(int *vi, int size): vi(vi), size(size) {}

  std::string toString();
};

class ImpureAttr : public AttrImpl<ImpureAttr, __LINE__> {
public:
  std::string toString() { return "<impure>"; }
};

}

#endif
