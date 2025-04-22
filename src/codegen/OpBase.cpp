#include "OpBase.h"
#include "Attrs.h"
#include <cassert>
#include <map>
#include <ostream>

using namespace sys;

std::map<BasicBlock*, int> sys::bbmap;
int sys::bbid = 0;

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

void BasicBlock::insert(iterator at, Op *op) {
  op->parent = this;
  op->place = ops.insert(at, op);
}

void BasicBlock::insertAfter(iterator at, Op *op) {
  op->parent = this;
  if (at == ops.end()) {
    ops.push_back(op);
    op->place = --end();
    return;
  }
  op->place = ops.insert(++at, op);
}

void BasicBlock::remove(iterator at) {
  ops.erase(at);
}

Op::Op(int id, const std::vector<Value> &values):
  id(id), result(this) {
  for (auto x : values) {
    operands.push_back(x);
    x.defining->uses.insert(this);
  }
}

void indent(std::ostream &os, int n) {
  for (int j = 0; j < n; j++)
    os << ' ';
}

Region *Op::appendRegion() {
  auto region = new Region(this);
  regions.push_back(region);
  return region;
}

void Op::setName(std::string name) {
  // Remove the final 'Op'
  name.pop_back();
  name.pop_back();
  for (auto &c : name)
    c = tolower(c);
  opname = name;
}

void Op::moveBefore(Op *op) {
  parent->remove(place);
  parent = op->parent;
  parent->insert(op->place, this);
}

void Op::moveAfter(Op *op) {
  parent->remove(place);
  parent = op->parent;
  parent->insertAfter(op->place, this);
}

void Op::moveToEnd(BasicBlock *block) {
  parent->remove(place);
  parent = block;
  parent->insert(parent->end(), this);
}

void Op::erase() {
  assert(uses.size() == 0);
  
  parent->remove(place);
  for (auto x : operands) {
    auto op = x.defining;
    op->uses.erase(this);
  }
  delete this;
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
    os << " " << attr->toString();
  if (regions.size() > 0) {
    os << " ";
    for (auto &region : regions)
      region->dump(os, depth + 1);
  }
  os << "\n";
}

void BasicBlock::moveAllOpsTo(BasicBlock *bb) {
  for (auto it = begin(); it != end(); ) {
    auto advanced = it; ++advanced;
    // `it` invalidates now.
    (*it)->moveToEnd(bb);
    it = advanced;
  }
}

BasicBlock *Region::insert(BasicBlock *at) {
  assert(at->parent == this);

  auto it = bbs.insert(at->place, nullptr);
  *it = new BasicBlock(this, it);
  return *it;
}

BasicBlock *Region::insertAfter(BasicBlock *at) {
  assert(at->parent == this);

  if (at->place == end())
    return appendBlock();

  auto place = at->place;
  ++place;
  auto it = bbs.insert(place, nullptr);
  *it = new BasicBlock(this, it);
  return *it;
}

BasicBlock *Region::appendBlock() {
  bbs.push_back(nullptr);
  auto place = --bbs.end();
  *place = new BasicBlock(this, place);
  return *place;
}

void Region::dump(std::ostream &os, int depth) {
  assert(depth >= 1);
  
  os << "{\n";
  for (auto it = bbs.begin(); it != bbs.end(); it++) {
    if (bbs.size() != 1) {
      indent(os, depth * 2 - 2);
      if (!bbmap.count(*it))
        bbmap[*it] = bbid++;
      os << "bb" << bbmap[*it] << ":\n";
    }
    auto &bb = *it;
    for (auto &x : bb->getOps())
      x->dump(os, depth);
  }
  indent(os, depth * 2 - 2);
  os << "}";
}
