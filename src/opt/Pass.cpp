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
