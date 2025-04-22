#include "CodeGen.h"
#include "../utils/DynamicCast.h"
#include "Attrs.h"
#include "OpBase.h"
#include <iostream>

using namespace sys;

void Builder::setToRegionStart(Region *region) {
  setToBlockStart(region->getFirstBlock());
}

void Builder::setToRegionEnd(Region *region) {
  setToBlockEnd(region->getFirstBlock());
}

void Builder::setToBlockStart(BasicBlock *block) {
  bb = block;
  at = bb->begin();
}

void Builder::setToBlockEnd(BasicBlock *block) {
  bb = block;
  at = bb->end();
}

void Builder::setBeforeOp(Op *op) {
  bb = op->parent;
  at = op->place;
}

void Builder::setAfterOp(Op *op) {
  setBeforeOp(op);
  ++at;
}

CodeGen::CodeGen(ASTNode *node): module(new ModuleOp()) {
  module->createFirstBlock();
  builder.setToRegionStart(module->getRegion());
  emit(node);
}

void CodeGen::emit(ASTNode *node) {
  if (auto block = dyn_cast<BlockNode>(node)) {
    for (auto x : block->nodes)
      emit(x);
  }

  if (auto fn = dyn_cast<FnDeclNode>(node)) {
    auto funcOp = builder.create<FuncOp>();
    funcOp->createFirstBlock();
    funcOp->addAttr<NameAttr>(fn->name);
  }
}
