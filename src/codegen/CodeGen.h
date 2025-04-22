#ifndef CODEGEN_H
#define CODEGEN_H

#include "OpBase.h"
#include "Ops.h"
#include "../main/Options.h"
#include "../parse/ASTNode.h"
#include <map>

namespace sys {

class Builder {
  BasicBlock *bb;
  BasicBlock::iterator at;
public:
  // Guards insertion point.
  struct Guard {
    Builder &builder;
    BasicBlock *bb;
    BasicBlock::iterator at;
  public:
    Guard(Builder &builder): builder(builder), bb(builder.bb), at(builder.at) {}
    ~Guard() { builder.bb = bb; builder.at = at; }
  };

  void setToRegionStart(Region *region);
  void setToRegionEnd(Region *region);
  void setToBlockStart(BasicBlock *block);
  void setToBlockEnd(BasicBlock *block);
  void setAfterOp(Op *op);
  void setBeforeOp(Op *op);

  template<class T>
  T *create(const std::vector<Value> &v) {
    auto op = new T(v);
    op->parent = bb;
    op->place = at;
    
    bb->insert(at, op);
    return op;
  }

  template<class T>
  T *create() {
    auto op = new T();
    op->parent = bb;
    op->place = at;
    
    bb->insert(at, op);
    return op;
  }

  template<class T>
  T *create(const std::vector<Attr*> &v) {
    auto op = new T(v);
    op->parent = bb;
    op->place = at;
    
    bb->insert(at, op);
    return op;
  }
};

class CodeGen {
  using SymbolTable = std::map<std::string, Value>;
  
  ModuleOp *module;
  Options opts;
  Builder builder;
  SymbolTable symbols;

  void emit(ASTNode *node);
  Value emitExpr(ASTNode *node);

  Value emitBinary(BinaryNode *node);
  
  int getSize(Type *ty);
public:
  class SemanticScope {
    CodeGen &cg;
    SymbolTable symbols;
  public:
    SemanticScope(CodeGen &cg): cg(cg), symbols(cg.symbols) {}
    ~SemanticScope() { cg.symbols = symbols; }
  };

  CodeGen(ASTNode *node);

  ModuleOp *getModule() { return module; }
};

}

#endif
