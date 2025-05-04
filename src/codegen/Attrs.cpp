#include "Attrs.h"
#include "Ops.h"
#include <sstream>

using namespace sys;

std::string TargetAttr::toString() {
  if (!bbmap.count(bb))
    bbmap[bb] = bbid++;
  return "<bb" + std::to_string(bbmap[bb]) + ">";
}

std::string ElseAttr::toString() {
  if (!bbmap.count(bb))
    bbmap[bb] = bbid++;
  return "<else = bb" + std::to_string(bbmap[bb]) + ">";
}

std::string FromAttr::toString() {
  if (!bbmap.count(bb))
    bbmap[bb] = bbid++;
  return "<from = bb" + std::to_string(bbmap[bb]) + ">";
}

IntArrayAttr::IntArrayAttr(int *vi, int size): vi(vi), size(size), allZero(true) {
  for (int i = 0; i < size; i++) {
    if (vi[i] != 0) {
      allZero = false;
      break;
    }
  }
}

std::string IntArrayAttr::toString() {
  if (allZero)
    return "<array = 0 x " + std::to_string(size) + ">";
  
  std::stringstream ss;
  ss << "<array = ";
  if (size > 0)
    ss << vi[0];
  for (int i = 1; i < size; i++)
    ss << ", " << vi[i];
  ss << ">";
  return ss.str();
}

std::string CallerAttr::toString() {
  std::stringstream ss;
  if (!callers.size())
    return "<no caller>";
  
  ss << "<caller = ";
  if (callers.size() > 0)
    ss << callers[0];
  for (int i = 1; i < callers.size(); i++)
    ss << ", " << callers[i];
  ss << ">";
  return ss.str();
}

// Implemented in OpBase.cpp.
std::ostream &operator<<(std::ostream&, Value);

std::string AliasAttr::toString() {
  std::stringstream ss;

  if (unknown)
    return "<alias = unknown>";

  if (location.size() == 0)
    return "<alias = none>";

  ss << "<alias = ";
  for (auto [base, offset] : location) {
    if (isa<GlobalOp>(base))
      ss << "global " << NAME(base);
    else
      ss << "alloca " << base->getResult();
    
    assert(offset.size() > 0);
    ss << ": " << offset[0];
    for (int i = 1; i < offset.size(); i++)
      ss << ", " << offset[i];
    
    ss << "; ";
  }

  auto str = ss.str();
  // Pop the last "; "
  str.pop_back();
  str.pop_back();
  // Push the final '>'
  str.push_back('>');
  return str;
}

bool AliasAttr::add(Op *base, int offset) {
  if (unknown)
    return false;

  // Known base, but unknown offset.
  if (offset == -1) {
    if (!location.count(base)) {
      location[base] = { -1 };
      return true;
    }
    auto &set = location[base];
    if (set.size() == 1 && set[0] == -1)
      return false;
    set = { -1 };
    return true;
  }

  // Both known base and known offset.
  auto &set = location[base];
  for (auto value : set) {
    if (value == offset)
      return false;
  }
  set.push_back(offset);
  return true;
}

bool AliasAttr::addAll(const AliasAttr *other) {
  if (unknown)
    return false;
  
  bool changed = false;
  for (auto [b, o] : other->location) {
    for (auto v : o)
      changed = add(b, v);
  }
  return changed;
}
