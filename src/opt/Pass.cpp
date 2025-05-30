#include "Pass.h"
#include "../codegen/Attrs.h"

using namespace sys;

bool sys::isExtern(const std::string &name) {
  static std::set<std::string> externs = {
    "getint",
    "getch",
    "getfloat",
    "getarray",
    "getfarray",
    "putint",
    "putch",
    "putfloat",
    "putarray",
    "putfarray",
    "_sysy_starttime",
    "_sysy_stoptime",
    "starttime",
    "stoptime",
  };
  return externs.count(name);
}

std::map<std::string, FuncOp*> Pass::getFunctionMap() {
  std::map<std::string, FuncOp*> funcs;

  auto region = module->getRegion();
  auto block = region->getFirstBlock();
  for (auto op : block->getOps()) {
    if (auto func = dyn_cast<FuncOp>(op))
      funcs[NAME(op)] = func;
  }
  
  return funcs;
}

std::map<std::string, GlobalOp*> Pass::getGlobalMap() {
  std::map<std::string, GlobalOp*> funcs;

  auto region = module->getRegion();
  auto block = region->getFirstBlock();
  for (auto op : block->getOps()) {
    if (auto glob = dyn_cast<GlobalOp>(op))
      funcs[NAME(op)] = glob;
  }
  
  return funcs;
}

std::vector<FuncOp*> Pass::collectFuncs() {
  std::vector<FuncOp*> result;
  auto toplevel = module->getRegion()->getFirstBlock()->getOps();
  for (auto op : toplevel) {
    if (auto fn = dyn_cast<FuncOp>(op))
      result.push_back(fn);
  }
  return result;
}

std::vector<GlobalOp*> Pass::collectGlobals() {
  std::vector<GlobalOp*> result;
  auto toplevel = module->getRegion()->getFirstBlock()->getOps();
  for (auto op : toplevel) {
    if (auto glob = dyn_cast<GlobalOp>(op))
      result.push_back(glob);
  }
  return result;
}

DomTree Pass::getDomTree(Region *region) {
  region->updateDoms();

  DomTree tree;
  for (auto bb : region->getBlocks()) {
    if (auto idom = bb->getIdom())
      tree[idom].push_back(bb);
  }
  return tree;
}

void Pass::cleanup() {
  Op::release();
  
  // Put phi's types right.
  runRewriter([&](PhiOp *op) {
    if (op->getResultType() == Value::f32)
      return false;

    for (auto operand : op->getOperands()) {
      if (operand.defining->getResultType() == Value::f32) {
        op->setResultType(Value::f32);
        return true;
      }
    }

    return false;
  });
}

Op *Pass::nonalloca(Region *region) {
  auto entry = region->getFirstBlock();
  Op *nonalloca = entry->getFirstOp();
  while (!nonalloca->atBack()) {
    if (isa<AllocaOp>(nonalloca))
      nonalloca = nonalloca->nextOp();
    else break;
  }
  if (nonalloca->atBack())
    nonalloca = entry->nextBlock()->getFirstOp();
  return nonalloca;
}

Op *Pass::nonphi(BasicBlock *bb) {
  Op *nonphi = bb->getFirstOp();
  while (!nonphi->atBack()) {
    if (isa<PhiOp>(nonphi))
      nonphi = nonphi->nextOp();
    else break;
  }
  // A basic block should have at least one op, so it's safe.
  return nonphi;
}
