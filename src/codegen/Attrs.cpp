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

FloatArrayAttr::FloatArrayAttr(float *vf, int size): vf(vf), size(size), allZero(true) {
  for (int i = 0; i < size; i++) {
    if (vf[i] != 0) {
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

std::string FloatArrayAttr::toString() {
  if (allZero)
    return "<array = 0.0f x " + std::to_string(size) + ">";
  
  std::stringstream ss;
  ss << "<array = ";
  if (size > 0)
    ss << vf[0] << "f";
  for (int i = 1; i < size; i++)
    ss << ", " << vf[i] << "f";
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
std::string getValueNumber(Value value);

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
      ss << "alloca " << getValueNumber(base->getResult());
    
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

std::string RangeAttr::toString() {
  auto [low, high] = range;
  if (low == INT_MIN && high == INT_MAX)
    return "<range = all>";
  if (low == high)
    return "<range = " + std::to_string(low) + ">";

  std::stringstream ss;
  ss << "<range = " << "[" << low << ", " << high << "]";
  ss << '>';
  return ss.str();
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
  for (auto &[b, o] : other->location) {
    for (auto v : o)
      changed = add(b, v);
  }
  return changed;
}

bool AliasAttr::mayAlias(const AliasAttr *other) const {
  return !neverAlias(other);
}

bool AliasAttr::mustAlias(const AliasAttr *other) const {
  if (unknown || other->unknown)
    return false;

  // When `this` must alias with `other`, then:
  //   1) they must have 1 key with 1 identical value;
  //   2) the value is not -1.
  if (this->location != other->location)
    return false;

  if (location.size() != 1)
    return false;

  auto &[base, offsets] = *location.begin();
  if (offsets.size() != 1)
    return false;

  return offsets[0] != -1;
}

bool AliasAttr::neverAlias(const AliasAttr *other) const {
  // When `this` never aliases with `other`, then:
  //   1) no identical (key, value) pairs can appear;
  //   2) no offset value of `-1` (unknown) appear on any of them.
  if (unknown || other->unknown)
    return false;

  for (auto &[base, offsets] : location) {
    // If `other` doesn't have this base, then no conflict at this base
    auto it = other->location.find(base);
    if (it == other->location.end())
      continue;

    // `it` points to a value type std::pair<key, value>.
    const std::vector<int> &offsets2 = it->second;

    // Check if any offset is -1.
    if (std::any_of(offsets.begin(), offsets.end(), [](int o) { return o < 0; }) ||
        std::any_of(offsets2.begin(), offsets2.end(), [](int o) { return o < 0; }))
      return false;

    // Check for any exact match.
    for (int o1 : offsets) {
      for (int o2 : offsets2)
        if (o1 == o2)
          return false;
    }
  }

  return true;
}

std::string IncreaseAttr::toString() {
  std::stringstream ss;
  ss << "<increase = ";
  if (amt.size() > 0)
    ss << amt[0];
  for (int i = 1; i < amt.size(); i++)
    ss << ", " << amt[i];
  if (mod != -1)
    ss << ", mod = " << mod;
  ss << ">";
  return ss.str();
}

bool sys::mustAlias(Op *a, Op *b) {
  if (a->has<AliasAttr>() && b->has<AliasAttr>())
    return ALIAS(a)->mustAlias(ALIAS(b));
  return false;
}

bool sys::neverAlias(Op *a, Op *b) {
  if (a->has<AliasAttr>() && b->has<AliasAttr>())
    return ALIAS(a)->neverAlias(ALIAS(b));
  return false;
}

bool sys::mayAlias(Op *a, Op *b) {
  return !neverAlias(a, b);
}
