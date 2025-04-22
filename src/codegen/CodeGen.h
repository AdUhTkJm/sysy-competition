#ifndef CODEGEN_H
#define CODEGEN_H

#include "OpBase.h"
#include "Ops.h"
#include "../main/Options.h"
#include "../parse/ASTNode.h"

namespace sys {

class Builder {
  BasicBlock *bb;
  BasicBlock::iterator at;
public:
  void setToRegionStart(Region *region);
  void setToRegionEnd(Region *region);
  void setToBlockStart(BasicBlock *block);
  void setToBlockEnd(BasicBlock *block);
  void setAfterOp(Op *op);
  void setBeforeOp(Op *op);

  template<class T, class... Args>
  T *create(Args... args) {
    auto op = new T(std::forward<Args>(args)...);
    op->parent = bb;
    op->place = at;
    
    bb->insert(at, op);
    return op;
  }
};

class CodeGen {
  // The operation for the whole translation unit.
  ModuleOp *module;
  Options opts;
  Builder builder;

  void emit(ASTNode *node);
public:
  CodeGen(ASTNode *node);

  ModuleOp *getModule() { return module; }
};

}

#endif
