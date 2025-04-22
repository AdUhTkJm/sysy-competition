#ifndef ATTRS_H
#define ATTRS_H

#include "OpBase.h"

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

class SizeAttr : public AttrImpl<SizeAttr, __LINE__> {
public:
  size_t value;

  SizeAttr(size_t value): value(value) {}

  std::string toString() { return "<size = " + std::to_string(value) + ">"; }
};

}

#endif
