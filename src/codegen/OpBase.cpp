#include "OpBase.h"
#include "Attrs.h"
#include <cassert>
#include <map>
#include <ostream>
#include <sstream>

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

std::string IntArrayAttr::toString() {
  std::stringstream ss("<array =");
  if (size > 0)
    ss << vi[0];
  for (int i = 1; i < size; i++)
    ss << ", " << vi[i];
  ss << ">";
  return ss.str();
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

Op *Op::getParentOp() {
  auto bb = parent;
  auto region = bb->parent;
  return region->getParent();
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

  // We can't delete Attr* because they'll be referenced elsewhere.
  // Memory leak? Who cares.
  delete this;
}

BasicBlock *Op::createFirstBlock() {
  appendRegion();
  return regions[0]->appendBlock();
}

void Op::replaceAllUsesWith(Op *other) {
  for (auto use : uses) {
    for (auto &operand : use->operands) {
      if (operand.defining != this)
        continue;

      operand.defining = other;
      other->uses.insert(use);
    }
  }
  uses.clear();
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

void BasicBlock::splitOpsAfter(BasicBlock *dest, Op *op) {
  for (auto it = op->place; it != end(); ) {
    auto advanced = it; ++advanced;
    // `it` invalidates now.
    (*it)->moveToEnd(dest);
    it = advanced;
  }
}

void BasicBlock::splitOpsBefore(BasicBlock *dest, Op *op) {
  for (auto it = begin(); it != op->place; ) {
    auto advanced = it; ++advanced;
    // `it` invalidates now.
    (*it)->moveToEnd(dest);
    it = advanced;
  }
}

void BasicBlock::moveBefore(BasicBlock *bb) {
  parent->remove(place);
  parent = bb->parent;
  parent->insert(bb->place, this);
}

void BasicBlock::moveAfter(BasicBlock *bb) {
  parent->remove(place);
  parent = bb->parent;
  parent->insertAfter(bb->place, this);
}

void BasicBlock::moveToEnd(Region *region) {
  parent->remove(place);
  parent = region;
  parent->insert(parent->end(), this);
}

void BasicBlock::erase() {
  assert(preds.size() == 0);
  
  parent->remove(place);
  delete this;
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

void Region::remove(BasicBlock *bb) {
  bbs.erase(bb->place);
}

void Region::remove(iterator at) {
  bbs.erase(at);
}

void Region::insert(iterator at, BasicBlock *bb) {
  bb->parent = this;
  bb->place = bbs.insert(at, bb);
}

void Region::insertAfter(iterator at, BasicBlock *bb) {
  bb->parent = this;
  if (at == bbs.end()) {
    bbs.push_back(bb);
    bb->place = --end();
    return;
  }
  bb->place = bbs.insert(++at, bb);
}

BasicBlock *Region::appendBlock() {
  bbs.push_back(nullptr);
  auto place = --bbs.end();
  *place = new BasicBlock(this, place);
  return *place;
}

std::pair<BasicBlock*, BasicBlock*> Region::moveTo(BasicBlock *bb) {
  BasicBlock *prev = bb;
  // Preserve it beforehand; the region will become empty afterwards
  auto result = std::make_pair(getFirstBlock(), getLastBlock());

  for (auto it = begin(); it != end(); ) {
    auto advanced = it; advanced++;
    auto current = *it;
    current->moveAfter(prev);
    prev = current;
    it = advanced;
  }

  return result;
}

void Region::updatePreds() {
  for (auto bb : bbs)
    bb->preds.clear();

  for (auto bb : bbs) {
    auto last = bb->getLastOp();
    if (last->hasAttr<TargetAttr>()) {
      auto target = last->getAttr<TargetAttr>();
      target->bb->preds.insert(bb);
    }

    if (last->hasAttr<ElseAttr>()) {
      auto ifnot = last->getAttr<ElseAttr>();
      ifnot->bb->preds.insert(bb);
    }
  }
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
