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

}

#endif
