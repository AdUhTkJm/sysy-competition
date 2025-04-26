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
  };
  return externs.count(name);
}

FuncOp *Pass::findFunction(const std::string &name) {
  std::map<std::string, FuncOp*> funcs;

  auto region = module->getRegion();
  auto block = region->getFirstBlock();
  for (auto op : block->getOps()) {
    if (auto func = dyn_cast<FuncOp>(op))
      funcs[op->getAttr<NameAttr>()->name] = func;
  }
  
  if (!funcs.count(name)) {
    std::cerr << "unknown function: " << name << "\n";
    assert(false);
  }
  return funcs[name];
}

PassManager::~PassManager() {
  for (auto pass : passes)
    delete pass;
}

void PassManager::setVerbose(bool verbose) {
  this->verbose = verbose;
}

void PassManager::setPrintStats(bool print) {
  this->print = print;
}

void PassManager::run() {
  for (auto pass : passes) {
    pass->run();

    if (verbose) {
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
