#include "CodeGen.h"
#include "../utils/DynamicCast.h"
#include "Attrs.h"
#include "OpBase.h"
#include "Ops.h"
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

int CodeGen::getSize(Type *ty) {
  assert(ty);
  if (isa<IntType>(ty) || isa<FloatType>(ty))
    return 4;
  if (auto arrTy = dyn_cast<ArrayType>(ty))
    return arrTy->getSize() * getSize(arrTy->base);
  
  return 8;
}

Value CodeGen::emitBinary(BinaryNode *node) {
  auto l = emitExpr(node->l);
  auto r = emitExpr(node->r);
  if (isa<IntType>(node->type)) {
    switch (node->kind) {
    case BinaryNode::Add:
      return builder.create<AddIOp>({ l, r });
    case BinaryNode::Sub:
      return builder.create<SubIOp>({ l, r });
    case BinaryNode::Mul:
      return builder.create<MulIOp>({ l, r });
    case BinaryNode::Div:
      return builder.create<DivIOp>({ l, r });
    case BinaryNode::Mod:
      return builder.create<ModIOp>({ l, r });
    case BinaryNode::Eq:
      return builder.create<EqOp>({ l, r });
    case BinaryNode::Ne:
      return builder.create<NeOp>({ l, r });
    case BinaryNode::Lt:
      return builder.create<LtOp>({ l, r });
    case BinaryNode::Le:
      return builder.create<LeOp>({ l, r });
    default:
      std::cerr << "unsupported binary " << node->kind << "\n";
      assert(false);
    }
  } else {
    assert(isa<FloatType>(node->type));
    switch (node->kind) {
    case BinaryNode::Add:
      return builder.create<AddFOp>({ l, r });
    case BinaryNode::Sub:
      return builder.create<SubFOp>({ l, r });
    case BinaryNode::Mul:
      return builder.create<MulFOp>({ l, r });
    case BinaryNode::Div:
      return builder.create<DivFOp>({ l, r });
    case BinaryNode::Mod:
      return builder.create<ModFOp>({ l, r });
    case BinaryNode::Eq:
      return builder.create<EqFOp>({ l, r });
    case BinaryNode::Ne:
      return builder.create<NeFOp>({ l, r });
    case BinaryNode::Lt:
      return builder.create<LtFOp>({ l, r });
    case BinaryNode::Le:
      return builder.create<LeFOp>({ l, r });
    default:
      std::cerr << "unsupported float binary " << node->kind << "\n";
      assert(false);
    }
  }
}

Value CodeGen::emitUnary(UnaryNode *node) {
  auto value = emitExpr(node->node);
  switch (node->kind) {
  case UnaryNode::Float2Int:
    return builder.create<F2IOp>({ value });
  case UnaryNode::Int2Float:
    return builder.create<I2FOp>({ value });
  case UnaryNode::Not:
    return builder.create<NotOp>({ value });
  case UnaryNode::Minus:
    if (isa<FloatType>(node->type))
      return builder.create<MinusFOp>({ value });
    else
      return builder.create<MinusOp>({ value });
  }
}

Value CodeGen::emitExpr(ASTNode *node) {
  if (auto binary = dyn_cast<BinaryNode>(node))
    return emitBinary(binary);
  
  if (auto unary = dyn_cast<UnaryNode>(node))
    return emitUnary(unary);

  if (auto lint = dyn_cast<IntNode>(node))
    return builder.create<IntOp>({ new IntAttr(lint->value) });

  if (auto lfloat = dyn_cast<FloatNode>(node))
    return builder.create<FloatOp>({ new FloatAttr(lfloat->value) });

  if (auto ref = dyn_cast<VarRefNode>(node)) {
    if (!symbols.count(ref->name)) {
      std::cerr << "cannot find symbol " << ref->name << "\n";
      assert(false);
    }
    auto from = symbols[ref->name];
    auto load = builder.create<LoadOp>({ from }, {
      new SizeAttr(getSize(ref->type))
    });
    return load;
  }
  
  if (auto call = dyn_cast<CallNode>(node)) {
    std::vector<Value> args;
    for (auto arg : call->args)
      args.push_back(emitExpr(arg));
    auto callOp = builder.create<CallOp>(args, {
      new NameAttr(call->func)
    });
    return callOp;
  }

  std::cerr << "cannot codegen node type " << node->getID() << "\n";
  assert(false);
}

void CodeGen::emit(ASTNode *node) {
  if (auto block = dyn_cast<BlockNode>(node)) {
    SemanticScope scope(*this);
    for (auto x : block->nodes)
      emit(x);
    return;
  }

  if (auto block = dyn_cast<TransparentBlockNode>(node)) {
    for (auto x : block->nodes)
      emit(x);
    return;
  }

  if (auto fn = dyn_cast<FnDeclNode>(node)) {
    auto funcOp = builder.create<FuncOp>();
    auto bb = funcOp->createFirstBlock();
    funcOp->addAttr<NameAttr>(fn->name);

    Builder::Guard guard(builder);
    builder.setToBlockStart(bb);

    // Function arguments are in the same scope with body.
    SemanticScope scope(*this);
    auto fnTy = cast<FunctionType>(fn->type);
    for (int i = 0; i < fn->args.size(); i++) {
      // Get the value of the argument and create a temp variable for it.
      auto arg = builder.create<GetArgOp>({ new IntAttr(i) });
      auto addr = builder.create<AllocaOp>({ new SizeAttr(getSize(fnTy->params[i])) });
      builder.create<StoreOp>({ arg, addr });
      symbols[fn->args[i]] = addr;
    }

    for (auto x : fn->body->nodes)
      emit(x);
    return;
  }

  if (auto vardecl = dyn_cast<VarDeclNode>(node)) {
    if (vardecl->global) {
      int *value = vardecl->init ? cast<ConstArrayNode>(vardecl->init)->vi : new int(0);
      auto size = 1;
      if (auto arrayTy = dyn_cast<ArrayType>(vardecl->type))
        size = arrayTy->getSize();
      
      auto addr = builder.create<GlobalOp>({
        new SizeAttr(getSize(vardecl->type)),
        new IntArrayAttr(value, size)
      });
      symbols[vardecl->name] = addr;
      return;
    }
    
    auto addr = builder.create<AllocaOp>({
        new SizeAttr(getSize(vardecl->type))
      });
    
    symbols[vardecl->name] = addr;
    if (vardecl->init) {
      // This is a local variable with array initializer.
      // We manually load everything into the array.
      if (auto arr = dyn_cast<LocalArrayNode>(vardecl->init)) {
        auto arrTy = cast<ArrayType>(vardecl->type);
        auto arrSize = arrTy->getSize();
        auto baseSize = getSize(arrTy->base);
        for (int i = 0; i < arrSize; i++) {
          Value value = arr->va[i]
            ? emitExpr(arr->va[i])
            : builder.create<IntOp>({ new IntAttr(0) });

          auto offset =  builder.create<IntOp>({ new IntAttr(baseSize * i) });
          auto place = builder.create<AddIOp>({ addr, offset });
          builder.create<StoreOp>({ value, place });
        }
        return;
      }

      auto value = emitExpr(vardecl->init);
      auto store = builder.create<StoreOp>({ value, addr });
      store->addAttr<SizeAttr>(getSize(vardecl->type));
    }
    return;
  }

  if (auto ret = dyn_cast<ReturnNode>(node)) {
    auto value = emitExpr(ret->node);
    builder.create<ReturnOp>({ value });
    return;
  }

  if (auto branch = dyn_cast<IfNode>(node)) {
    auto cond = emitExpr(branch->cond);
    auto op = builder.create<IfOp>({ cond });

    auto thenBlock = op->createFirstBlock();
    {
      Builder::Guard guard(builder);
      builder.setToBlockStart(thenBlock);
      emit(branch->ifso);
    }
    if (branch->ifnot) {
      auto elseRegion = op->appendRegion();
      auto elseBlock = elseRegion->appendBlock();
      Builder::Guard guard(builder);
      builder.setToBlockStart(elseBlock);
      emit(branch->ifnot);
    }
    return;
  }

  if (auto loop = dyn_cast<WhileNode>(node)) {
    // Imitate the design of scf.while.
    // The `condRegion` is the `before` region, and the last op of it is ProceedOp;
    // Only when the operand of ProceedOp is true, the `after` region is executed,
    // which is called `bodyRegion` here.
    auto op = builder.create<WhileOp>();
    auto condRegion = op->createFirstBlock();

    {
      Builder::Guard guard(builder);
      builder.setToBlockStart(condRegion);
      auto cond = emitExpr(loop->cond);
      builder.create<ProceedOp>({ cond });
    }
    auto bodyRegion = op->appendRegion();
    auto bodyBlock = bodyRegion->appendBlock();

    Builder::Guard guard(builder);
    builder.setToBlockStart(bodyBlock);
    emit(loop->body);
    return;
  }

  if (auto assign = dyn_cast<AssignNode>(node)) {
    auto l = cast<VarRefNode>(assign->l);
    auto addr = symbols[l->name];
    auto value = emitExpr(assign->r);
    builder.create<StoreOp>({ value, addr });
    return;
  }
  
  emitExpr(node);
}
