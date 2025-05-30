#include "CodeGen.h"
#include "../utils/DynamicCast.h"
#include "Attrs.h"
#include "OpBase.h"
#include "Ops.h"
#include <cstring>
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
  init = true;
}

void Builder::setToBlockEnd(BasicBlock *block) {
  bb = block;
  at = bb->end();
  init = true;
}

void Builder::setBeforeOp(Op *op) {
  bb = op->parent;
  at = op->place;
  init = true;
}

void Builder::setAfterOp(Op *op) {
  setBeforeOp(op);
  ++at;
}

Op *Builder::copy(Op *op) {
  auto opnew = new Op(op->opid, op->resultTy, op->operands);
  for (auto attr : op->attrs) {
    auto cloned = attr->clone();
    cloned->refcnt++;
    opnew->attrs.push_back(cloned);
  }
  opnew->opname = op->opname;
  bb->insert(at, opnew);
  return opnew;
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
    return getSize(arrTy->base) * arrTy->getSize();
  
  return 8;
}

Value CodeGen::emitBinary(BinaryNode *node) {
  // TODO: for float, we need to test zero in a different way.
  if (node->kind == BinaryNode::And) {
    auto alloca = builder.create<AllocaOp>({ new SizeAttr(4) });
    //   l && r
    // becomes
    //   if (l)
    //     %1 = not_zero r
    //     store %1, %alloca
    //   else
    //     store 0, %alloca
    //   load %alloca
    auto l = emitExpr(node->l);
    auto branch = builder.create<IfOp>({ l });
    {
      auto ifso = branch->appendRegion();
      auto block = ifso->appendBlock();
      Builder::Guard guard(builder);

      builder.setToBlockStart(block);
      auto r = emitExpr(node->r);
      auto snez = builder.create<SetNotZeroOp>({ r });
      builder.create<StoreOp>({ snez, alloca }, { new SizeAttr(4) });
    }
    {
      auto ifnot = branch->appendRegion();
      auto block = ifnot->appendBlock();
      Builder::Guard guard(builder);

      builder.setToBlockStart(block);
      auto zero = builder.create<IntOp>({ new IntAttr(0) });
      builder.create<StoreOp>({ zero, alloca }, { new SizeAttr(4) });
    }
    return builder.create<LoadOp>(Value::i32, { alloca }, { new SizeAttr(4) });
  }

  if (node->kind == BinaryNode::Or) {
    auto alloca = builder.create<AllocaOp>({ new SizeAttr(4) });
    //   l || r
    // becomes
    //   if (l)
    //     store 1, %alloca
    //   else
    //     %1 = not_zero r
    //     store %1, %alloca
    //   load %alloca
    auto l = emitExpr(node->l);
    auto branch = builder.create<IfOp>({ l });
    {
      auto ifso = branch->appendRegion();
      auto block = ifso->appendBlock();
      Builder::Guard guard(builder);

      builder.setToBlockStart(block);
      auto one = builder.create<IntOp>({ new IntAttr(1) });
      builder.create<StoreOp>({ one, alloca }, { new SizeAttr(4) });
    }
    {
      auto ifnot = branch->appendRegion();
      auto block = ifnot->appendBlock();
      Builder::Guard guard(builder);

      builder.setToBlockStart(block);
      auto r = emitExpr(node->r);
      auto sez = builder.create<SetNotZeroOp>({ r });
      builder.create<StoreOp>({ sez, alloca }, { new SizeAttr(4) });
    }
    return builder.create<LoadOp>(Value::i32, { alloca }, { new SizeAttr(4) });
  }

  auto l = emitExpr(node->l);
  auto r = emitExpr(node->r);
  if (!isa<FloatType>(node->l->type) && !isa<FloatType>(node->r->type)) {
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
      assert(false);
    }
  } else {
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
  assert(false);
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
    bool isFloat = isa<FloatType>(node->type);
    Value::Type resultTy = isFloat ? Value::f32 : Value::i32;

    if (!symbols.count(ref->name)) {
      if (globals.count(ref->name)) {
        auto addr = builder.create<GetGlobalOp>({
          new NameAttr(ref->name)
        });
        // No extra indirection for global arrays.
        if (isa<ArrayType>(ref->type) || isa<PointerType>(ref->type))
          return addr;

        auto load = builder.create<LoadOp>(resultTy, { addr }, {
          new SizeAttr(getSize(ref->type))
        });
        return load;
      }

      std::cerr << "cannot find symbol " << ref->name << "\n";
      assert(false);
    }
    auto from = symbols[ref->name];
    auto load = builder.create<LoadOp>(resultTy, { from }, {
      new SizeAttr(getSize(ref->type))
    });
    return load;
  }
  
  if (auto call = dyn_cast<CallNode>(node)) {
    std::vector<Value> args;
    for (auto arg : call->args)
      args.push_back(emitExpr(arg));

    // Note that "starttime" and "stoptime" are actually "_sysy_{start,stop}time".
    auto name = call->func;
    if (name == "starttime")
      name = "_sysy_starttime";
    if (name == "stoptime")
      name = "_sysy_stoptime";

    bool isFP = isa<FloatType>(call->type);
    auto callOp = builder.create<CallOp>(isFP ? Value::f32 : Value::i32, args, {
      new NameAttr(name),
    });
    return callOp;
  }

  if (auto access = dyn_cast<ArrayAccessNode>(node)) {
    auto arrTy = cast<ArrayType>(access->arrTy);

    // Calculate a series of stride.
    std::vector<int> sizes;
    auto size = getSize(arrTy->base) * arrTy->getSize();
    for (int i = 0; i < arrTy->dims.size(); i++)
      sizes.push_back(size /= arrTy->dims[i]);
    
    Value addr;
    if (symbols.count(access->array))
      addr = builder.create<LoadOp>(Value::i64, {
        symbols[access->array]
      }, { new SizeAttr(8) });
    else if (globals.count(access->array))
      addr = builder.create<GetGlobalOp>({
        new NameAttr(access->array)
      });
    else {
      std::cerr << "unknown array: " << access->array << "\n";
      assert(false);
    }
    for (int i = 0; i < access->indices.size(); i++) {
      auto index = emitExpr(access->indices[i]);
      auto strideVal = builder.create<IntOp>({ new IntAttr(sizes[i]) });
      auto stride = builder.create<MulIOp>({ index, strideVal });
      addr = builder.create<AddLOp>({ addr, stride });
    }
    // This is not a value, but just an address.
    // Directly return the address (for, e.g. function arguments)
    if (arrTy->dims.size() > access->indices.size())
      return addr;

    // Store the value in addr.
    bool isFP = isa<FloatType>(arrTy->base);
    return builder.create<LoadOp>(isFP ? Value::f32 : Value::i32, { addr }, {
      new SizeAttr(getSize(arrTy->base))
    });
  }

  std::cerr << "cannot codegen node type " << node->getID() << "\n";
  assert(false);
}

void CodeGen::emit(ASTNode *node) {
  if (isa<EmptyNode>(node))
    return;
  
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
    auto fnTy = cast<FunctionType>(fn->type);
    auto funcOp = builder.create<FuncOp>({
      new NameAttr(fn->name),
      new ArgCountAttr(fnTy->params.size())
    });
    auto bb = funcOp->createFirstBlock();

    Builder::Guard guard(builder);
    builder.setToBlockStart(bb);

    // Function arguments are in the same scope with body.
    SemanticScope scope(*this);
    for (int i = 0; i < fn->args.size(); i++) {
      auto argTy = fnTy->params[i];
      auto size = getSize(argTy);
      // Get the value of the argument and create a temp variable for it.
      Value::Type ty = isa<FloatType>(argTy) ? Value::f32 : Value::i32;
      auto arg = builder.create<GetArgOp>(ty, { new IntAttr(i) });
      if (isa<ArrayType>(argTy) || isa<PointerType>(argTy))
        funcOp->add<ImpureAttr>();

      auto addr = builder.create<AllocaOp>({ new SizeAttr(size) });
      builder.create<StoreOp>({ arg, addr }, { new SizeAttr(size) });
      // Mark address as floating point if necessary.
      if (isa<FloatType>(argTy))
        addr->add<FPAttr>();
      
      symbols[fn->args[i]] = addr;
    }

    for (auto x : fn->body->nodes)
      emit(x);
    return;
  }

  if (auto vardecl = dyn_cast<VarDeclNode>(node)) {
    if (vardecl->global) {
      if (vardecl->init && isa<IntNode>(vardecl->init)) {
        int value = cast<IntNode>(vardecl->init)->value;
        // Treat the single integer as an array.
        auto addr = builder.create<GlobalOp>({
          new SizeAttr(getSize(vardecl->type)),
          new IntArrayAttr(new int(value), 1),
          new NameAttr(vardecl->name),
        });
        globals[vardecl->name] = addr;
        return;
      }

      if (vardecl->init && isa<FloatNode>(vardecl->init)) {
        float value = cast<FloatNode>(vardecl->init)->value;
        // Treat the single integer as an array.
        auto addr = builder.create<GlobalOp>({
          new SizeAttr(getSize(vardecl->type)),
          new FloatArrayAttr(new float(value), 1),
          new NameAttr(vardecl->name),
        });
        globals[vardecl->name] = addr;
        return;
      }

      auto size = 1;
      Type *base = vardecl->type;
      if (auto arrTy = dyn_cast<ArrayType>(vardecl->type)) {
        size = arrTy->getSize();
        base = arrTy->base;
      }

      void *value;
      if (vardecl->init)
        value = isa<FloatType>(base)
          ? (void*) cast<ConstArrayNode>(vardecl->init)->vf
          : (void*) cast<ConstArrayNode>(vardecl->init)->vi;
      else {
        value = isa<FloatType>(base)
          ? (void*) new float[size]
          : (void*) new int[size];
        // sizeof(int) == sizeof(float).
        memset(value, 0, sizeof(int) * size);
      }
      
      Value addr;
      if (isa<FloatType>(base)) {
        addr = builder.create<GlobalOp>({
          new SizeAttr(getSize(vardecl->type)),
          new FloatArrayAttr((float*) value, size),
          new NameAttr(vardecl->name),
        });
        addr.defining->add<FPAttr>();
      } else {
        addr = builder.create<GlobalOp>({
          new SizeAttr(getSize(vardecl->type)),
          new IntArrayAttr((int*) value, size),
          new NameAttr(vardecl->name),
        });
      }
      globals[vardecl->name] = addr;
      return;
    }
    
    auto addr = builder.create<AllocaOp>({
      new SizeAttr(getSize(vardecl->type))
    });
    if (isa<FloatType>(vardecl->type))
      addr->add<FPAttr>();
    
    symbols[vardecl->name] = addr;
    // An uninitialized local array.
    // Give it another alloca.
    if (isa<ArrayType>(vardecl->type) && !vardecl->init) {
      auto arrayPtr = builder.create<AllocaOp>({
        new SizeAttr(8)
      });
      builder.create<StoreOp>({ addr, arrayPtr }, { new SizeAttr(8) });
      symbols[vardecl->name] = arrayPtr;
      // Also check whether this is a floating point array.
      // If it is, give the original alloca a FPAttr.
      auto arrTy = cast<ArrayType>(vardecl->type);
      if (isa<FloatType>(arrTy->base))
        addr->add<FPAttr>();
      return;
    }

    if (vardecl->init) {
      // This is a local variable with array initializer.
      // We manually load everything into the array.
      if (auto arr = dyn_cast<LocalArrayNode>(vardecl->init)) {
        auto arrTy = cast<ArrayType>(vardecl->type);
        auto base = arrTy->base;
        auto arrSize = arrTy->getSize();
        auto baseSize = getSize(arrTy->base);
        int zeroFrom = arrSize - 1;
        for (; zeroFrom >= 0; zeroFrom--) {
          if (arr->va[zeroFrom])
            break;
        }
        // Now [zeroFrom + 1, arrSize) are all zeroes.
        // We don't want to create too many stores. Create a loop instead.
        int max = arrSize - zeroFrom >= 16384 ? zeroFrom : arrSize;
        for (int i = 0; i < max; i++) {
          Value value = arr->va[i]
            ? emitExpr(arr->va[i])
            : isa<FloatType>(base)
              ? (Value) builder.create<FloatOp>({ new FloatAttr(0) })
              : (Value) builder.create<IntOp>({ new IntAttr(0) });

          auto offset =  builder.create<IntOp>({ new IntAttr(baseSize * i) });
          auto place = builder.create<AddLOp>({ addr, offset });
          builder.create<StoreOp>({ value, place }, { new SizeAttr(baseSize) });
        }
        if (max != arrSize) {
          auto start = builder.create<IntOp>({ new IntAttr(zeroFrom + 1) });
          auto end = builder.create<IntOp>({ new IntAttr(arrSize) });
          auto iv = builder.create<AllocaOp>({ new SizeAttr(4) });
          auto zero = isa<FloatType>(base)
              ? (Value) builder.create<FloatOp>({ new FloatAttr(0) })
              : (Value) builder.create<IntOp>({ new IntAttr(0) });
          auto stride = builder.create<IntOp>({ new IntAttr(baseSize) });
          auto incr = builder.create<IntOp>({ new IntAttr(1) });

          auto loop = builder.create<ForOp>({ start, end, incr, iv });
          auto body = loop->appendRegion();
          body->appendBlock();
          
          builder.setToRegionStart(body);

          auto offset = builder.create<MulIOp>({ loop, stride });
          auto place = builder.create<AddLOp>({ addr, offset });
          builder.create<StoreOp>({ zero, place });
        }

        // An extra layer of indirection is needed for further reference.
        auto arrayPtr = builder.create<AllocaOp>({
          new SizeAttr(8)
        });
        builder.create<StoreOp>({ addr, arrayPtr }, { new SizeAttr(8) });
        symbols[vardecl->name] = arrayPtr;
        // Give a FPAttr if the array is float*.
        if (isa<FloatType>(arrTy->base))
          addr->add<FPAttr>();
        return;
      }

      auto value = emitExpr(vardecl->init);
      auto store = builder.create<StoreOp>({ value, addr });
      store->add<SizeAttr>(getSize(vardecl->type));
    }
    return;
  }

  if (auto ret = dyn_cast<ReturnNode>(node)) {
    if (!ret->node) {
      builder.create<ReturnOp>();
      return;
    }
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
  
  if (isa<BreakNode>(node)) {
    builder.create<BreakOp>();
    return;
  }

  if (isa<ContinueNode>(node)) {
    builder.create<ContinueOp>();
    return;
  }

  if (auto assign = dyn_cast<AssignNode>(node)) {
    auto l = cast<VarRefNode>(assign->l);
    Value addr;
    if (symbols.count(l->name))
      addr = symbols[l->name];
    else if (globals.count(l->name))
      addr = builder.create<GetGlobalOp>({
        new NameAttr(l->name)
      });
    else {
      std::cerr << "assign to unknown name: " << l->name << "\n";
      assert(false);
    }
    auto value = emitExpr(assign->r);
    builder.create<StoreOp>({ value, addr }, {
      new SizeAttr(getSize(assign->l->type))
    });
    return;
  }

  if (auto write = dyn_cast<ArrayAssignNode>(node)) {
    auto value = emitExpr(write->value);
    auto arrTy = cast<ArrayType>(write->arrTy);

    // Calculate a series of stride.
    std::vector<int> sizes;
    auto size = getSize(arrTy->base) * arrTy->getSize();
    for (int i = 0; i < write->indices.size(); i++)
      sizes.push_back(size /= arrTy->dims[i]);
    
    Value addr;
    if (symbols.count(write->array))
      addr = builder.create<LoadOp>(Value::i64, {
        symbols[write->array]
      }, { new SizeAttr(8) });
    else if (globals.count(write->array))
      addr = builder.create<GetGlobalOp>({
        new NameAttr(write->array)
      });
    else {
      std::cerr << "assign to unknown name: " << write->array << "\n";
      assert(false);
    }
    for (int i = 0; i < write->indices.size(); i++) {
      auto index = emitExpr(write->indices[i]);
      auto strideVal = builder.create<IntOp>({ new IntAttr(sizes[i]) });
      auto stride = builder.create<MulIOp>({ index, strideVal });
      addr = builder.create<AddLOp>({ addr, stride });
    }
    // Store the value in addr.
    builder.create<StoreOp>({ value, addr }, {
      new SizeAttr(getSize(arrTy->base))
    });
    return;
  }
  
  emitExpr(node);
}
