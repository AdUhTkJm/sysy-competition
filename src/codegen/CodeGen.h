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
  bool init = false;
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

  // We use 8 overloads because literal { ... } can't be deduced.
  template<class T>
  T *create(const std::vector<Value> &v) {
    assert(init);
    auto op = new T(v);
    bb->insert(at, op);
    return op;
  }

  template<class T>
  T *create() {
    assert(init);
    auto op = new T();
    bb->insert(at, op);
    return op;
  }

  template<class T>
  T *create(const std::vector<Attr*> &v) {
    assert(init);
    auto op = new T(v);
    bb->insert(at, op);
    return op;
  }

  template<class T>
  T *create(const std::vector<Value> &v, const std::vector<Attr*> &v2) {
    assert(init);
    auto op = new T(v, v2);
    bb->insert(at, op);
    return op;
  }

  template<class T>
  T *create(Value::Type resultTy, const std::vector<Value> &v) {
    assert(init);
    auto op = new T(resultTy, v);
    bb->insert(at, op);
    return op;
  }

  template<class T>
  T *create(Value::Type resultTy) {
    assert(init);
    auto op = new T(resultTy);
    bb->insert(at, op);
    return op;
  }

  template<class T>
  T *create(Value::Type resultTy, const std::vector<Attr*> &v) {
    assert(init);
    auto op = new T(resultTy, v);
    bb->insert(at, op);
    return op;
  }

  template<class T>
  T *create(Value::Type resultTy, const std::vector<Value> &v, const std::vector<Attr*> &v2) {
    assert(init);
    auto op = new T(resultTy, v, v2);
    bb->insert(at, op);
    return op;
  }

  // Similarly 8 more, replacing `op` with the newly constructed one.
  template<class T>
  T *replace(Op *op, const std::vector<Value> &v) {
    setBeforeOp(op);
    auto opnew = create<T>(v);
    op->replaceAllUsesWith(opnew);
    op->erase();
    return opnew;
  }

  template<class T>
  T *replace(Op *op) {
    setBeforeOp(op);
    auto opnew = create<T>();
    op->replaceAllUsesWith(opnew);
    op->erase();
    return opnew;
  }

  template<class T>
  T *replace(Op *op, const std::vector<Attr*> &v) {
    setBeforeOp(op);
    auto opnew = create<T>(v);
    op->replaceAllUsesWith(opnew);
    op->erase();
    return opnew;
  }

  template<class T>
  T *replace(Op *op, const std::vector<Value> &v, const std::vector<Attr*> &v2) {
    setBeforeOp(op);
    auto opnew = create<T>(v, v2);
    op->replaceAllUsesWith(opnew);
    op->erase();
    return opnew;
  }

  template<class T>
  T *replace(Op *op, Value::Type resultTy, const std::vector<Value> &v) {
    setBeforeOp(op);
    auto opnew = create<T>(resultTy, v);
    op->replaceAllUsesWith(opnew);
    op->erase();
    return opnew;
  }

  template<class T>
  T *replace(Op *op, Value::Type resultTy) {
    setBeforeOp(op);
    auto opnew = create<T>(resultTy);
    op->replaceAllUsesWith(opnew);
    op->erase();
    return opnew;
  }

  template<class T>
  T *replace(Op *op, Value::Type resultTy, const std::vector<Attr*> &v) {
    setBeforeOp(op);
    auto opnew = create<T>(resultTy, v);
    op->replaceAllUsesWith(opnew);
    op->erase();
    return opnew;
  }

  template<class T>
  T *replace(Op *op, Value::Type resultTy, const std::vector<Value> &v, const std::vector<Attr*> &v2) {
    setBeforeOp(op);
    auto opnew = create<T>(resultTy, v, v2);
    op->replaceAllUsesWith(opnew);
    op->erase();
    return opnew;
  }

  // This shallow-copies operands, but deep copies attributes.
  Op *copy(Op *op);
};

class CodeGen {
  using SymbolTable = std::map<std::string, Value>;
  
  ModuleOp *module;
  Options opts;
  Builder builder;
  SymbolTable symbols;
  SymbolTable globals;

  void emit(ASTNode *node);
  Value emitExpr(ASTNode *node);

  Value emitBinary(BinaryNode *node);
  Value emitUnary(UnaryNode *node);
  
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
  CodeGen(const CodeGen &other) = delete;

  ModuleOp *getModule() { return module; }
};

}

#endif
