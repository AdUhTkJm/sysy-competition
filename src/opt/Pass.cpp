#include "Pass.h"
#include "../codegen/Attrs.h"

#include <iostream>

using namespace sys;

FuncOp *Pass::findFunction(const std::string &name) {
  static std::map<std::string, FuncOp*> cache;

  if (cache.empty()) {
    auto region = module->getRegion();
    auto block = region->getFirstBlock();
    for (auto op : block->getOps()) {
      if (auto func = dyn_cast<FuncOp>(op))
        cache[op->getAttr<NameAttr>()->name] = func;
    }
  }
  
  return cache.count(name) ? cache[name] : nullptr;
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
      std::cout << "===== After " << pass->name() << " =====\n\n";
      module->dump(std::cout);
      std::cout << "\n\n";
    }
    
    if (print) {
      std::cout << pass->name() << ":\n";

      auto stats = pass->stats();
      if (!stats.size())
        std::cout << "  <no stats>\n";

      for (auto [k, v] : stats)
        std::cout << "  " << k << " : " << v << "\n";
    }
  }
}
