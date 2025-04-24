#include "Sema.h"
#include "ASTNode.h"
#include "../utils/DynamicCast.h"
#include "Type.h"

#include <cassert>
#include <iostream>
#include <vector>

using namespace sys;

Type *Sema::infer(ASTNode *node) {
  if (auto fn = dyn_cast<FnDeclNode>(node)) {
    assert(fn->type);
    auto fnTy = cast<FunctionType>(fn->type);
    symbols[fn->name] = fn->type;

    SemanticScope scope(*this);
    for (int i = 0; i < fn->args.size(); i++) {
      symbols[fn->args[i]] = fnTy->params[i];
    }

    for (auto x : fn->body->nodes)
      infer(x);
    return ctx.create<VoidType>();
  }

  if (auto block = dyn_cast<BlockNode>(node)) {
    SemanticScope scope(*this);
    for (auto x : block->nodes)
      infer(x);
    return node->type = ctx.create<VoidType>();
  }

  if (auto block = dyn_cast<TransparentBlockNode>(node)) {
    for (auto x : block->nodes)
      infer(x);
    return node->type = ctx.create<VoidType>();
  }

  if (isa<IntNode>(node))
    return node->type = ctx.create<IntType>();

  if (isa<FloatNode>(node))
    return node->type = ctx.create<FloatType>();
  
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

  if (auto vardecl = dyn_cast<VarDeclNode>(node)) {
    assert(node->type);
    symbols[vardecl->name] = node->type;
    if (!vardecl->init)
      return ctx.create<VoidType>();

    if (vardecl->global || !vardecl->mut)
      // Already folded. Just propagate type.
      vardecl->init->type = node->type;
    else
      infer(vardecl->init);
    return ctx.create<VoidType>();
  }

  if (auto ret = dyn_cast<ReturnNode>(node)) {
    infer(ret->node);
    return ctx.create<VoidType>();
  }
  
  if (auto ref = dyn_cast<VarRefNode>(node)) {
    if (!symbols.count(ref->name)) {
      std::cerr << "cannot find symbol " << ref->name << "\n";
      assert(false);
    }
    return node->type = symbols[ref->name];
  }

  if (auto branch = dyn_cast<IfNode>(node)) {
    auto condTy = infer(branch->cond);
    if (!isa<IntType>(condTy)) {
      std::cerr << "bad cond type\n";
      assert(false);
    }
    infer(branch->ifso);
    if (branch->ifnot)
      infer(branch->ifnot);
    return node->type = ctx.create<VoidType>();
  }

  if (auto loop = dyn_cast<WhileNode>(node)) {
    auto condTy = infer(loop->cond);
    if (!isa<IntType>(condTy)) {
      std::cerr << "bad cond type\n";
      assert(false);
    }
    infer(loop->body);
    return node->type = ctx.create<VoidType>();
  }

  if (auto assign = dyn_cast<AssignNode>(node)) {
    auto lty = infer(assign->l);
    auto rty = infer(assign->r);
    if (lty != rty) {
      std::cerr << "bad assign op\n";
      assert(false);
    }
    return node->type = ctx.create<VoidType>();
  }

  if (isa<ConstArrayNode>(node) || isa<LocalArrayNode>(node)) {
    assert(node->type);
    return node->type;
  }

  if (auto call = dyn_cast<CallNode>(node)) {
    for (auto x : call->args)
      infer(x);
    if (!symbols.count(call->func)) {
      std::cerr << "cannot find function " << call->func << "\n";
      assert(false);
    }
    auto fnTy = cast<FunctionType>(symbols[call->func]);
    return node->type = fnTy->ret;
  }

  std::cerr << "cannot infer node " << node->getID() << "\n";
  assert(false);
}

Sema::Sema(ASTNode *node, TypeContext &ctx): ctx(ctx) {
  auto intTy = ctx.create<IntType>();
  auto floatTy = ctx.create<FloatType>();
  auto voidTy = ctx.create<VoidType>();
  auto intPtrTy = ctx.create<PointerType>(intTy);
  auto floatPtrTy = ctx.create<PointerType>(floatTy);

  using Args = std::vector<Type*>;
  Args empty;
  
  // Internal library.
  symbols = {
    { "getint", ctx.create<FunctionType>(intTy, empty) },
    { "getch", ctx.create<FunctionType>(intTy, empty) },
    { "getfloat", ctx.create<FunctionType>(floatTy, empty) },
    { "getarray", ctx.create<FunctionType>(intTy, Args { intPtrTy }) },
    { "getfarray", ctx.create<FunctionType>(intTy, Args { floatPtrTy } ) },
    { "putint", ctx.create<FunctionType>(voidTy, Args { intTy }) },
    { "putch", ctx.create<FunctionType>(voidTy, Args { intTy }) },
    { "putfloat", ctx.create<FunctionType>(voidTy, Args { floatTy }) },
    { "putarray", ctx.create<FunctionType>(voidTy, Args { intTy, intPtrTy }) },
    { "putfarray", ctx.create<FunctionType>(voidTy, Args { intTy, floatPtrTy }) },
  };

  infer(node);
}
