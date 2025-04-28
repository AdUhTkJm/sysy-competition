#include "Sema.h"
#include "ASTNode.h"
#include "../utils/DynamicCast.h"
#include "Type.h"

#include <cassert>
#include <iostream>
#include <vector>
#include <set>

using namespace sys;

PointerType *Sema::decay(ArrayType *arrTy) {
  std::vector<int> dims;
  for (int i = 1; i < arrTy->dims.size(); i++)
    dims.push_back(arrTy->dims[i]);
  if (!dims.size())
    return ctx.create<PointerType>(arrTy->base);
  return ctx.create<PointerType>(ctx.create<ArrayType>(arrTy->base, dims));
}

ArrayType *Sema::raise(PointerType *ptr) {
  std::vector<int> dims { 1 };
  Type *base;
  if (auto pointee = dyn_cast<ArrayType>(ptr->pointee)) {
    for (auto x : pointee->dims)
      dims.push_back(x);
    base = pointee->base;
  } else
    base = ptr->pointee;
  return ctx.create<ArrayType>(base, dims);
}

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

    if (isa<FloatType>(lty) && isa<IntType>(rty)) {
      binary->r = new UnaryNode(UnaryNode::Int2Float, binary->r);
      binary->r->type = ctx.create<FloatType>();
      return binary->type = ctx.create<FloatType>();
    }

    if (isa<IntType>(lty) && isa<FloatType>(rty)) {
      binary->l = new UnaryNode(UnaryNode::Int2Float, binary->l);
      binary->l->type = ctx.create<FloatType>();
      return binary->type = ctx.create<FloatType>();
    }

    std::set<decltype(binary->kind)> intops {
      BinaryNode::And,
      BinaryNode::Or,
      BinaryNode::Eq,
      BinaryNode::Ne,
      BinaryNode::Le,
      BinaryNode::Lt,
    };
    if (isa<FloatType>(lty) && isa<FloatType>(rty) && !intops.count(binary->kind))
      return node->type = ctx.create<FloatType>();

    if (lty != rty) {
      std::cerr << "bad binary op\n";
      assert(false);
    }
    
    return node->type = ctx.create<IntType>();
  }

  if (auto unary = dyn_cast<UnaryNode>(node)) {
    auto ty = infer(unary->node);
    // These two ops won't be emitted in parser.
    assert(unary->kind != UnaryNode::Float2Int && unary->kind != UnaryNode::Int2Float);

    if (isa<FloatType>(ty) && unary->kind == UnaryNode::Minus)
      return node->type = ctx.create<FloatType>();
    
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
    else {
      auto ty = infer(vardecl->init);
      if (isa<IntType>(ty) && isa<FloatType>(vardecl->type)) {
        vardecl->init = new UnaryNode(UnaryNode::Int2Float, vardecl->init);
        vardecl->init->type = ctx.create<FloatType>();
        return ctx.create<VoidType>();
      }

      if (isa<FloatType>(ty) && isa<IntType>(vardecl->type)) {
        vardecl->init = new UnaryNode(UnaryNode::Float2Int, vardecl->init);
        vardecl->init->type = ctx.create<IntType>();
        return ctx.create<VoidType>();
      }

      if (ty != vardecl->type) {
        std::cerr << "bad assignment\n";
        assert(false);
      }
    }
    return ctx.create<VoidType>();
  }

  if (auto ret = dyn_cast<ReturnNode>(node)) {
    auto ty = infer(ret->node);
    if (!symbols.count(ret->func)) {
      std::cerr << "cannot find symbol " << ret->func << "\n";
      assert(false);
    }

    auto retTy = cast<FunctionType>(symbols[ret->func])->ret;
    if (isa<IntType>(retTy) && isa<FloatType>(ty)) {
      ret->node = new UnaryNode(UnaryNode::Float2Int, ret->node);
      ret->node->type = ctx.create<FloatType>();
      return ctx.create<VoidType>();
    }

    if (isa<FloatType>(retTy) && isa<IntType>(ty)) {
      ret->node = new UnaryNode(UnaryNode::Int2Float, ret->node);
      ret->node->type = ctx.create<IntType>();
      return ctx.create<VoidType>();
    }

    if (retTy != ret->node->type) {
      std::cerr << "bad return\n";
      assert(false);
    }
    return ctx.create<VoidType>();
  }
  
  if (auto ref = dyn_cast<VarRefNode>(node)) {
    if (!symbols.count(ref->name)) {
      std::cerr << "cannot find symbol " << ref->name << "\n";
      assert(false);
    }
    auto ty = symbols[ref->name];

    // Decay.
    if (auto arrTy = dyn_cast<ArrayType>(ty))
      return node->type = decay(arrTy);
    
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
    if (isa<IntType>(lty) && isa<FloatType>(rty)) {
      assign->r = new UnaryNode(UnaryNode::Int2Float, assign->r);
      assign->r->type = ctx.create<FloatType>();
      return ctx.create<VoidType>();
    }

    if (isa<FloatType>(lty) && isa<IntType>(rty)) {
      assign->r = new UnaryNode(UnaryNode::Float2Int, assign->r);
      assign->r->type = ctx.create<IntType>();
      return ctx.create<VoidType>();
    }

    if (lty != rty) {
      std::cerr << "bad assignment\n";
      assert(false);
    }
    return node->type = ctx.create<VoidType>();
  }

  if (isa<ConstArrayNode>(node)) {
    assert(node->type);
    return node->type;
  }

  if (auto arr = dyn_cast<LocalArrayNode>(node)) {
    assert(node->type);
    auto arrTy = cast<ArrayType>(node->type);
    auto baseTy = arrTy->base;
    auto size = arrTy->getSize();
    for (int i = 0; i < size; i++) {
      auto &x = arr->va[i];
      if (!x)
        continue;
      auto ty = infer(x);

      if (isa<FloatType>(ty) && isa<IntType>(baseTy)) {
        x = new UnaryNode(UnaryNode::Float2Int, x);
        x->type = ctx.create<IntType>();
        continue;
      }

      if (isa<IntType>(ty) && isa<FloatType>(baseTy)) {
        x = new UnaryNode(UnaryNode::Int2Float, x);
        x->type = ctx.create<FloatType>();
        continue;
      }
    }
    return node->type;
  }

  if (auto call = dyn_cast<CallNode>(node)) {
    auto fnTy = cast<FunctionType>(symbols[call->func]);
    for (size_t i = 0; i < fnTy->params.size(); i++) {
      ASTNode *&x = call->args[i];
      auto ty = infer(x);
      auto argTy = fnTy->params[i];

      if (isa<FloatType>(ty) && isa<IntType>(argTy)) {
        x = new UnaryNode(UnaryNode::Float2Int, x);
        x->type = ctx.create<IntType>();
        continue;
      }

      if (isa<IntType>(ty) && isa<FloatType>(argTy)) {
        x = new UnaryNode(UnaryNode::Int2Float, x);
        x->type = ctx.create<FloatType>();
        continue;
      }

      // Both pointers / arrays.
    }
    if (!symbols.count(call->func)) {
      std::cerr << "cannot find function " << call->func << "\n";
      assert(false);
    }
    return node->type = fnTy->ret;
  }

  if (auto access = dyn_cast<ArrayAccessNode>(node)) {
    auto realTy = symbols[access->array];
    ArrayType *arrTy;
    if (isa<ArrayType>(realTy))
      arrTy = cast<ArrayType>(realTy);
    else
      arrTy = raise(cast<PointerType>(realTy));
    
    access->arrTy = arrTy;
    std::vector<int> dimsNew;
    for (int i = access->indices.size(); i < arrTy->dims.size(); i++)
      dimsNew.push_back(arrTy->dims[i]);
    
    for (auto x : access->indices) {
      auto ty = infer(x);
      assert(isa<IntType>(ty));
    }

    auto resultTy = dimsNew.size()
      ? (Type*) decay(ctx.create<ArrayType>(arrTy->base, dimsNew))
      : arrTy->base;
    return node->type = resultTy;
  }

  if (auto write = dyn_cast<ArrayAssignNode>(node)) {
    auto realTy = symbols[write->array];
    ArrayType *arrTy;
    if (isa<ArrayType>(realTy))
      arrTy = cast<ArrayType>(realTy);
    else
      arrTy = raise(cast<PointerType>(realTy));

    auto baseTy = arrTy->base;
    assert(write->indices.size() == arrTy->dims.size());
    write->arrTy = arrTy;

    for (auto x : write->indices) {
      auto ty = infer(x);
      assert(isa<IntType>(ty));
    }

    auto valueTy = infer(write->value);

    if (isa<FloatType>(baseTy) && isa<IntType>(valueTy)) {
      write->value = new UnaryNode(UnaryNode::Float2Int, write->value);
      write->value->type = ctx.create<IntType>();
    }

    if (isa<IntType>(baseTy) && isa<FloatType>(valueTy)) {
      write->value = new UnaryNode(UnaryNode::Int2Float, write->value);
      write->value->type = ctx.create<FloatType>();
    }

    return node->type = ctx.create<VoidType>();
  }

  std::cerr << "cannot infer node " << node->getID() << "\n";
  assert(false);
}

Sema::Sema(ASTNode *node, TypeContext &ctx): ctx(ctx) {
  auto intTy = ctx.create<IntType>();
  auto floatTy = ctx.create<FloatType>();
  auto voidTy = ctx.create<VoidType>();
  auto intPtrTy = ctx.create<ArrayType>(intTy, std::vector { 1 });
  auto floatPtrTy = ctx.create<ArrayType>(floatTy, std::vector { 1 });

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
