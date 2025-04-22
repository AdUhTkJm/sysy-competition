#include "Pass.h"

using namespace sys;

PassManager::~PassManager() {
  for (auto pass : passes)
    delete pass;
}

void PassManager::run() {
  for (auto pass : passes)
    pass->run();
}

template<class T>
void Rewriter<T>::run(Op *op) {
  if (auto t = dyn_cast<T>(op))
    rewrite(t);
  
  for (auto region : op->getRegions()) {
    for (auto bb : region->getBlocks()) {
      for (auto x : bb->getOps())
        run(x);
    }
  }
}
