#include "Pass.h"
#include "../codegen/Attrs.h"

#include <iostream>

using namespace sys;

bool Pass::isExtern(const std::string &name) {
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

PassManager::~PassManager() {
  for (auto pass : passes)
    delete pass;
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

void PassManager::setVerbose(bool verbose) {
  this->verbose = verbose;
}

void PassManager::setPrintStats(bool print) {
  this->print = print;
}

void PassManager::setPrintAfter(const std::string &printAfter) {
  this->printAfter = printAfter;
}

void PassManager::run() {
  for (auto pass : passes) {
    pass->run();

    if (verbose || pass->name() == printAfter) {
      std::cerr << "===== After " << pass->name() << " =====\n\n";
      module->dump(std::cerr);
      std::cerr << "\n\n";
    }
    
    if (print) {
      std::cerr << pass->name() << ":\n";

      auto stats = pass->stats();
      if (!stats.size())
        std::cerr << "  <no stats>\n";

      for (auto [k, v] : stats)
        std::cerr << "  " << k << " : " << v << "\n";
    }
  }
}
