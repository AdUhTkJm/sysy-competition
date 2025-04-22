#include "OpBase.h"
#include <cassert>
#include <map>
#include <ostream>

using namespace sys;

Op::Op(int id, const std::vector<Value> &values):
  id(id), result(this) {
  for (auto x : values) {
    operands.push_back(x);
    x.defining->uses.push_back(this);
  }
}

void indent(std::ostream &os, int n) {
  for (int j = 0; j < n; j++)
    os << ' ';
}

void Op::appendRegion() {
  regions.push_back(new Region());
}

void Op::setName(std::string name) {
  // Remove the final 'Op'
  name.pop_back();
  name.pop_back();
  for (auto &c : name)
    c = tolower(c);
  opname = name;
}

BasicBlock *Op::createFirstBlock() {
  appendRegion();
  return regions[0]->appendBlock();
}

static std::map<Op*, int> valueName = {};
static int id = 0;

std::ostream &operator<<(std::ostream &os, Value &value) {
  if (!valueName.count(value.defining))
    valueName[value.defining] = id++;
  return os << "%" << valueName[value.defining];
}

void Op::dump(std::ostream &os, int depth) {
  indent(os, depth * 2);
  os << result << " = " << opname;
  for (auto &operand : operands)
    os << " " << operand;
  for (auto attr : attrs)
    os << " " << attr->toString() << " ";
  if (regions.size() > 0) {
    os << " ";
    for (auto &region : regions)
      region->dump(os, depth + 1);
  }
  os << "\n";
}

BasicBlock *Region::appendBlock() {
  auto bb = new BasicBlock();
  bbs.push_back(bb);
  return bb;
}

void Region::dump(std::ostream &os, int depth) {
  assert(depth >= 1);
  
  os << "{\n";
  int count = 0;
  for (auto it = bbs.begin(); it != bbs.end(); it++) {
    if (bbs.size() != 1) {
      indent(os, depth * 2 - 2);
      os << "bb" << count++ << ":";
    }
    auto &bb = *it;
    for (auto &x : bb->getOps())
      x->dump(os, depth + 1);
  }
  indent(os, depth * 2 - 2);
  os << "}";
}
