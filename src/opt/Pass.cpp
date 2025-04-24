#include "Pass.h"
#include <iostream>

using namespace sys;

PassManager::~PassManager() {
  for (auto pass : passes)
    delete pass;
}

void PassManager::setVerbose(bool verbose) {
  this->verbose = verbose;
}

void PassManager::run() {
  for (auto pass : passes) {
    pass->run();
    
    if (verbose) {
      std::cout << pass->name() << ":\n";

      auto stats = pass->stats();
      if (!stats.size())
        std::cout << "  <no stats>\n";

      for (auto [k, v] : stats)
        std::cout << "  " << k << " : " << v << "\n";
    }
  }
}
