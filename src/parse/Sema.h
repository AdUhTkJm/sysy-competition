#ifndef SEMA_H
#define SEMA_H

#include "ASTNode.h"
#include "Type.h"
#include "TypeContext.h"
#include <map>
#include <string>

namespace sys {

// We don't need to do type inference, hence no memory management needed
class Sema {
  TypeContext &ctx;
  // The current function we're in. Mainly used for deducing return type.
  Type *currentFunc;

  using SymbolTable = std::map<std::string, Type*>;
  SymbolTable symbols;

  class SemanticScope {
    Sema &sema;
    SymbolTable symbols;
  public:
    SemanticScope(Sema &sema): sema(sema), symbols(sema.symbols) {}
    ~SemanticScope() { sema.symbols = symbols; }
  };

  PointerType *decay(ArrayType *arrTy);
  ArrayType *raise(PointerType *ptr);

  Type *infer(ASTNode *node);
public:
  // This modifies `node` inplace.
  Sema(ASTNode *node, TypeContext &ctx);
};

}

#endif
