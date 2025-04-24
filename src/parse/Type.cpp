#include <sstream>

#include "Type.h"

using namespace sys;

std::string interleave(const std::vector<Type*> &types) {
  std::stringstream ss;
  for (auto x : types)
    ss << x->toString() << ", ";
  auto str = ss.str();
  // Remove the extra ", " at the end
  if (str.size() > 2) {
    str.pop_back();
    str.pop_back();
  }
  return str;
}

std::string FunctionType::toString() const {
  return "(" + interleave(params) + ") -> " + ret->toString();
}

std::string ArrayType::toString() const {
  std::stringstream ss(base->toString());
  for (auto x : dims)
    ss << "[" << x << "]";
  return ss.str();
}

int ArrayType::getSize() const {
  int size = 1;
  for (auto x : dims)
    size *= x;
  return size;
}
