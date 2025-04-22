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
