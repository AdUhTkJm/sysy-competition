#include "Sema.h"
#include "ASTNode.h"
#include "../utils/DynamicCast.h"

#include <cassert>
#include <iostream>

using namespace sys;

Type *Sema::infer(ASTNode *node) {
  if (node->type)
    return node->type;

  if (isa<IntNode>(node))
    return node->type = ctx.create<IntType>();
  
  if (auto binary = dyn_cast<BinaryNode>(node)) {
    auto lty = infer(binary->l);
    auto rty = infer(binary->r);
    if (lty != rty) {
      std::cerr << "bad binary op\n";
      assert(false);
    }
    // TODO: deal with float & implicit cast
    return node->type = ctx.create<IntType>();
  }

  // Not every node has a type (consider block node etc.)
  return nullptr;
}

Sema::Sema(ASTNode *node, TypeContext &ctx): ctx(ctx) {
  infer(node);
}
