#include "OpBase.h"
#include "Attrs.h"
#include "Ops.h"

#include <algorithm>
#include <cassert>
#include <deque>
#include <iterator>
#include <map>
#include <iostream>
#include <string>

using namespace sys;

std::map<BasicBlock*, int> sys::bbmap;
int sys::bbid = 0;

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

BasicBlock *BasicBlock::nextBlock() {
  auto it = place;
  return *++it;
}

Op *Op::prevOp() {
  auto it = place;
  return *--it;
}

Op *Op::nextOp() {
  auto it = place;
  return *++it;
}

Value::Value(Op *from):
  defining(from), ty(from->getResultType()) {}

Op::Op(int id, Value::Type resultTy, const std::vector<Value> &values):
  id(id), resultTy(resultTy) {
  for (auto x : values) {
    operands.push_back(x);
    x.defining->uses.insert(this);
  }
}

Op::Op(int id, Value::Type resultTy, const std::vector<Value> &values, const std::vector<Attr*> &attrs):
  id(id), resultTy(resultTy) {
  for (auto x : values) {
    operands.push_back(x);
    x.defining->uses.insert(this);
  }
  for (auto attr : attrs) {
    auto cloned = attr->clone();
    this->attrs.push_back(cloned);
    cloned->refcnt++;
    if (!attr->refcnt)
      delete attr;
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

void Op::pushOperand(Value v) {
  v.defining->uses.insert(this);
  operands.push_back(v);
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

void Op::removeAllOperands() {
  for (auto x : operands) {
    auto op = x.defining;
    op->uses.erase(this);
  }
  operands.clear();
}

void Op::removeAllAttributes() {
  for (auto attr : attrs) {
    if (!--attr->refcnt)
      delete attr;
  }
  attrs.clear();
}

void Op::removeRegion(Region *region) {
  for (auto it = regions.begin(); it != regions.end(); it++) {
    if (*it == region) {
      regions.erase(it);
      break;
    }
  }
}

void Op::setOperand(int i, Value v) {
  auto def = operands[i].defining;
  operands[i] = v;
  def->uses.erase(this);
  v.defining->uses.insert(this);
}

void Op::replaceOperand(Op *before, Value v) {
  for (auto &x : operands) {
    auto def = x.defining;
    if (def == before) {
      x = v;
      def->uses.erase(this);
      v.defining->uses.insert(this);
      return;
    }
  }
  assert(false);
}

void Op::erase() {
  if (uses.size()) {
    std::cerr << "removing op in use:\n  ";
    dump(std::cerr);
    std::cerr << "uses:\n";
    for (auto use : uses) {
      std::cerr << "  ";
      use->dump(std::cerr);
    }
    assert(false);
  }
  
  parent->remove(place);
  removeAllOperands();

  for (auto region : regions)
    region->erase();
  
  for (auto attr : attrs) {
    if (!--attr->refcnt)
      delete attr;
  }
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

std::ostream &operator<<(std::ostream &os, Value value) {
  if (!valueName.count(value.defining))
    valueName[value.defining] = id++;
  return os << "%" << valueName[value.defining];
}

void Op::dump(std::ostream &os, int depth) {
  indent(os, depth * 2);
  os << getResult() << " = " << opname;
  if (resultTy == Value::f32)
    os << ".f";
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
  if (preds.size() != 0) {
    std::cerr << "Erasing block with preds!\nself = bb" << bbmap[this] << "; all preds: ";
    for (auto x : preds)
      std::cerr << "bb" << bbmap[x] << " ";
    std::cerr << "\n";
    assert(false);
  }
  
  forceErase();
}

void BasicBlock::forceErase() {
  auto copy = ops;
  for (auto op : ops)
    op->removeAllOperands();
  for (auto op : copy)
    op->erase();
  
  parent->remove(place);
  delete this;
}

std::vector<Op*> BasicBlock::getPhis() {
  std::vector<Op*> phis;
  for (auto op : ops) {
    if (!isa<PhiOp>(op))
      break;

    phis.push_back(op);
  }
  return phis;
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

void Region::erase() {
  for (auto bb : bbs) {
    for (auto op : bb->getOps()) {
      op->removeAllOperands();
      for (auto region : op->getRegions())
        region->erase();
    }
  }
  auto copy = bbs;
  for (auto bb : copy)
    bb->forceErase();
  parent->removeRegion(this);
  delete this;
}

void Region::updatePreds() {
  for (auto bb : bbs) {
    bb->preds.clear();
    bb->succs.clear();
    bb->reachables.clear();
  }

  for (auto bb : bbs) {
    assert(bb->getOps().size() > 0);
    auto last = bb->getLastOp();
    if (last->has<TargetAttr>()) {
      auto target = last->get<TargetAttr>();
      target->bb->preds.insert(bb);
    }

    if (last->has<ElseAttr>()) {
      auto ifnot = last->get<ElseAttr>();
      ifnot->bb->preds.insert(bb);
    }
  }

  for (auto bb : bbs) {
    for (auto pred : bb->getPreds())
      pred->succs.insert(bb);
    bb->reachables.insert(bb);
  }

  bool changed;
  do {
    changed = false;
    for (auto bb : bbs) {
      auto old = bb->reachables;
      for (auto x : bb->reachables) {
        for (auto succ : x->succs) {
          auto [it, absent] = bb->reachables.insert(succ);
          changed |= absent;
        }
      }
    }
  } while (changed);
}

// Use the simple data-flow approach, rather than the Tarjan one.
// See https://en.wikipedia.org/wiki/Dominator_(graph_theory)
void Region::updateDoms() {
  updatePreds();
  // Clear existing data.
  for (auto bb : bbs) {
    bb->doms.clear();
    bb->idom = nullptr;
    bb->domFront.clear();
  }

  for (auto x : bbs)
    std::copy(bbs.begin(), bbs.end(), std::inserter(x->doms, x->doms.end()));
  
  auto start = getFirstBlock();
  start->doms.clear();
  start->doms.insert(start);

  bool changed;
  do {
    changed = false;
    for (auto x : bbs) {
      if (x == start)
        continue;

      // Don't forget to set the identity to the full bbs.
      std::set<BasicBlock*> result;
      std::copy(bbs.begin(), bbs.end(), std::inserter(result, result.end()));

      for (auto pred : x->preds) {
        std::set<BasicBlock*> temp;
        std::set_intersection(pred->doms.begin(), pred->doms.end(), result.begin(), result.end(),
          std::inserter(temp, temp.end()));
        result = std::move(temp);
      }

      result.insert(x);
      if (x->doms != result) {
        changed = true;
        x->doms = result;
      }
    }
  } while (changed);

  for (auto bb : bbs) {
    // Start block has no idom
    if (bb == start)
      continue;
    
    const auto &doms = bb->doms;
    for (auto candidate : doms) {
      if (candidate == bb)
        continue;

      bool isIdom = true;
      for (auto other : doms) {
        if (other == bb || other == candidate)
          continue;
        if (other->doms.count(candidate)) { 
          // `candidate` dominates another block, so not immediate
          isIdom = false;
          break;
        }
      }
      if (isIdom) {
        bb->idom = candidate;
        break;
      }
    }
    // Only blocks without preds can have no idom.
    // We must remove those blocks before calling `updateDoms`.
    assert(bb->idom);
  }

  // Update dominance frontier.
  for (auto bb : bbs)
    bb->domFront.clear();

  // See https://en.wikipedia.org/wiki/Static_single-assignment_form#Computing_minimal_SSA_using_dominance_frontiers
  // For each block, if it has at least 2 preds, then it must be at dominance frontier of all its `preds`,
  // till its `idom`.
  for (auto bb : bbs) {
    if (bb->preds.size() < 2)
      continue;

    for (auto pred : bb->preds) {
      auto runner = pred;
      while (runner != bb->idom) {
        runner->domFront.insert(bb);
        runner = runner->idom;
      }
    }
  }
}

// A dual of updateDoms().
void Region::updatePDoms() {
  updatePreds();
  for (auto bb : bbs) {
    bb->postdoms.clear();
    bb->ipdom = nullptr;
    bb->postdomFront.clear();
  }

  for (auto x : bbs)
    std::copy(bbs.begin(), bbs.end(), std::inserter(x->postdoms, x->postdoms.end()));
  
  // We assume the last block is the exit block.
  auto end = getLastBlock();
  assert(isa<ReturnOp>(end->getLastOp()));
  end->postdoms.clear();
  end->postdoms.insert(end);

  bool changed;
  do {
    changed = false;
    for (auto x : bbs) {
      if (x == end)
        continue;

      // Don't forget to set the identity to the full bbs.
      std::set<BasicBlock*> result;
      std::copy(bbs.begin(), bbs.end(), std::inserter(result, result.end()));

      // Duality: changed to successors in this function.
      for (auto succ : x->succs) {
        std::set<BasicBlock*> temp;
        std::set_intersection(succ->postdoms.begin(), succ->postdoms.end(), result.begin(), result.end(),
          std::inserter(temp, temp.end()));
        result = std::move(temp);
      }

      result.insert(x);
      if (x->postdoms != result) {
        changed = true;
        x->postdoms = result;
      }
    }
  } while (changed);

  for (auto bb : bbs) {
    // Start block has no idom
    if (bb == end)
      continue;
    
    const auto &postdoms = bb->postdoms;
    for (auto candidate : postdoms) {
      if (candidate == bb)
        continue;

      bool isIdom = true;
      for (auto other : postdoms) {
        if (other == bb || other == candidate)
          continue;
        if (other->postdoms.count(candidate)) { 
          // `candidate` dominates another block, so not immediate
          isIdom = false;
          break;
        }
      }
      if (isIdom) {
        bb->ipdom = candidate;
        break;
      }
    }
    // Only blocks without successors can have no idom.
    // We must remove those blocks before calling `updateDoms`.
    assert(bb->ipdom);
  }

  // Update dominance frontier.
  for (auto bb : bbs)
    bb->postdomFront.clear();

  // See https://en.wikipedia.org/wiki/Static_single-assignment_form#Computing_minimal_SSA_using_dominance_frontiers
  // For each block, if it has at least 2 preds, then it must be at dominance frontier of all its `preds`,
  // till its `idom`.
  for (auto bb : bbs) {
    if (bb->succs.size() < 2)
      continue;

    for (auto succ : bb->succs) {
      auto runner = succ;
      while (runner != bb->ipdom) {
        runner->postdomFront.insert(bb);
        runner = runner->ipdom;
      }
    }
  }
}

// See the SSA Book:
//   https://pfalcon.github.io/ssabook/latest/book-full.pdf
// Page 116.
void Region::updateLiveness() {
  updatePreds();

  // Clear existing values.
  for (auto bb : bbs) {
    bb->liveIn.clear();
    bb->liveOut.clear();
  }

  std::map<BasicBlock*, std::set<Op*>> phis;
  std::map<BasicBlock*, std::set<Op*>> upwardExposed;
  std::map<BasicBlock*, std::set<Op*>> defined;

  for (auto bb : bbs) {
    for (auto op : bb->getOps()) {
      if (isa<PhiOp>(op)) {
        phis[bb].insert(op);
        continue;
      }

      defined[bb].insert(op);

      // A value is upward exposed if it's from some block upwards;
      // i.e. it's used but not defined in this block.
      for (auto value : op->getOperands()) {
        if (!defined[bb].count(value.defining))
          upwardExposed[bb].insert(value.defining);
      }
    }
  }

  std::deque<BasicBlock*> worklist;
  
  // Do a dataflow approach. We start with all exit blocks;
  // i.e. those that have no successors.
  std::copy_if(bbs.begin(), bbs.end(), std::back_inserter(worklist), [&](BasicBlock *bb) {
    return bb->getSuccs().size() == 0;
  });

  bool changed;
  do {
    changed = false;
    for (auto bb : bbs) {
      auto liveInOld = bb->liveIn;

      // LiveOut(B) = \bigcup_{S\in succ(B)} (LiveIn(S) - PhiDefs(S)) \cup PhiUses(B)
      // Here PhiUses(B) means the set of variables used in Phi nodes of S that come from B.
      std::set<Op*> liveOut;
      for (auto succ : bb->getSuccs()) {
        std::set_difference(
          succ->liveIn.begin(), succ->liveIn.end(),
          phis[succ].begin(), phis[succ].end(),
          std::inserter(liveOut, liveOut.end())
        );
        for (auto phi : phis[succ]) {
          auto &ops = phi->getOperands();
          auto &attrs = phi->getAttrs();
          for (size_t i = 0; i < ops.size(); i++) {
            if (FROM(attrs[i]) == bb)
              liveOut.insert(ops[i].defining);
          }
        }
      }

      bb->liveOut = liveOut;

      // LiveIn(B) = PhiDefs(B) \cup UpwardExposed(B) \cup (LiveOut(B) - Defs(B))
      bb->liveIn.clear();
      std::set_difference(
        liveOut.begin(), liveOut.end(),
        defined[bb].begin(), defined[bb].end(),
        std::inserter(bb->liveIn, bb->liveIn.end())
      );
      for (auto x : upwardExposed[bb])
        bb->liveIn.insert(x);
      for (auto x : phis[bb])
        bb->liveIn.insert(x);

      if (liveInOld != bb->liveIn)
        changed = true;
    }
  } while (changed);

  // showLiveIn();
}

void Region::showLiveIn() {
  std::cerr << "===== live info starts =====\n";
  for (auto bb : bbs) {
    std::cerr << "=== block ===\n";
    for (auto x : bb->getOps()) {
      std::cerr << "  ";
      x->dump(std::cerr);
    }
    std::cerr << "=== livein ===\n";
    for (auto x : bb->liveIn) {
      std::cerr << "  ";
      x->dump(std::cerr);
    }
    std::cerr << "=== liveout ===\n";
    for (auto x : bb->liveOut) {
      std::cerr << "  ";
      x->dump(std::cerr);
    }
    std::cerr << "\n\n";
  }
  std::cerr << "===== live info ends =====\n\n\n";
}

static int getBlockID(BasicBlock *bb) {
  if (!bbmap.count(bb))
    bbmap[bb] = bbid++;
  return bbmap[bb];
}

void Region::dump(std::ostream &os, int depth) {
  assert(depth >= 1);
  
  os << "{\n";
  for (auto it = bbs.begin(); it != bbs.end(); it++) {
    if (bbs.size() != 1) {
      auto bb = *it;
      indent(os, depth * 2 - 2);
      os << "bb" << getBlockID(bb) << ":     // preds = [ ";
      for (auto x : bb->getPreds())
        os << getBlockID(x) << " ";
      
      os << "]; dom frontier = [ ";
      for (auto x : bb->getDominanceFrontier())
        os << getBlockID(x) << " ";
      
      os << "]";
      
      if (bb->idom)
        os << "; idom = " << getBlockID(bb->idom);
      os << "\n";
    }
    auto &bb = *it;
    for (auto &x : bb->getOps())
      x->dump(os, depth);
  }
  indent(os, depth * 2 - 2);
  os << "}";
}
