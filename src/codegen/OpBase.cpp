#include "OpBase.h"
#include <cassert>
#include <memory>
#include <ostream>

using namespace sys;

int Value::id = 0;

void indent(std::ostream &os, int n) {
  for (int j = 0; j < n; j++)
    os << ' ';
}

void Op::appendRegion() {
  regions.push_back(std::make_unique<Region>());
}

void Op::dump(std::ostream &os, int depth) {
  indent(os, depth * 2);
  os << opname;
  for (auto operand : operands)
    os << " " << "%" << operand.name;
  if (regions.size() > 0) {
    for (auto &region : regions)
      region->dump(os, depth + 1);
    return;
  }
  os << ";";
}

void Region::dump(std::ostream &os, int depth) {
  assert(depth >= 1);
  
  os << '{';
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
  os << '}';
}
