#ifndef SEMA_H
#define SEMA_H

#include "ASTNode.h"
#include "TypeContext.h"
#include <map>
#include <string>

namespace sys {

// We don't need to do type inference, hence no memory management needed
class Sema {
  TypeContext &ctx;

  using SymbolTable = std::map<std::string, Type*>;
  SymbolTable symbols;

  class SemanticScope {
    Sema &sema;
    SymbolTable symbols;
  public:
    SemanticScope(Sema &sema): sema(sema), symbols(sema.symbols) {}
    ~SemanticScope() { sema.symbols = symbols; }
  };

  Type *infer(ASTNode *node);
public:
  // This modifies `node` inplace.
  Sema(ASTNode *node, TypeContext &ctx);
};

}

#endif
