#include "Passes.h"

using namespace sys;

void MoveAlloca::run() {
  auto funcs = module->findAll<FuncOp>();
  
  for (auto func : funcs) {
    auto allocas = func->findAll<AllocaOp>();
    auto region = func->getRegion();
    auto begin = region->insert(region->getFirstBlock());
    for (auto alloca : allocas)
      alloca->moveToEnd( begin);
    
  }
}
