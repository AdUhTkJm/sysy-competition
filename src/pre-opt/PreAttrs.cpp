#include "PreAttrs.h"
#include <sstream>

using namespace sys;

std::string SubscriptAttr::toString() {
  std::stringstream ss;
  ss << "<subscript = ";
  if (subscript.size() > 0)
    ss << subscript[0];
  for (int i = 1; i < subscript.size(); i++)
    ss << ", " << subscript[i];
  ss << ">";
  return ss.str();
}

// Defined in OpBase.cpp.
std::string getValueNumber(Value value);

std::string BaseAttr::toString() {
  std::stringstream ss;
  ss << "<base = " << getValueNumber(base->getResult()) << ">";
  return ss.str();
}

std::string ParallelizableAttr::toString() {
  std::stringstream ss;
  if (!accum)
    return "<parallelizable>";
  ss << "<parallel accum = " << getValueNumber(accum->getResult()) << ">";
  return ss.str();
}
