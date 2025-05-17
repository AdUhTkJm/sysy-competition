#include "Exec.h"
#include "../codegen/Attrs.h"
#include <cstring>

using namespace sys;
using namespace sys::exec;

#define sys_unreachable(x) \
  do { std::cerr << x << "\n"; assert(false); } while (0)

Interpreter::Interpreter(ModuleOp *module) {
  auto region = module->getRegion();
  auto block = region->getFirstBlock();
  for (auto op : block->getOps()) {
    if (auto glob = dyn_cast<GlobalOp>(op)) {
      const auto &name = NAME(op);
      int size = SIZE(op) / 4;
      
      if (auto intArr = op->find<IntArrayAttr>()) {
        int *vp = new int[size];
        memcpy(vp, intArr->vi, size * 4);
        globalMap[name] = Value { .vi = (int64_t) vp };
      }
      if (auto fpArr = op->find<FloatArrayAttr>()) {
        float *vfp = new float[size];
        memcpy(vfp, fpArr->vf, size * 4);
        globalMap[name] = Value { .vi = (int64_t) vfp };
        fpGlobals.insert(name);
      }
      continue;
    }

    if (auto fn = dyn_cast<FuncOp>(op)) {
      fnMap[NAME(op)] = op;
      continue;
    }
    
    sys_unreachable("unexpected top level op: " << op);
  }
}

Interpreter::~Interpreter() {
  for (const auto &[name, ptr] : globalMap) {
    // Hopefully this won't violate strict aliasing rule.
    if (fpGlobals.count(name))
      delete[] ((float*) ptr.vi);
    else
      delete[] ((int*) ptr.vi);
  }
}

intptr_t Interpreter::eval(Op *op) {
  assert(value.count(op) && op->getResultType() != sys::Value::f32);
  return value[op].vi;
}

float Interpreter::evalf(Op *op) {
  assert(value.count(op) && op->getResultType() == sys::Value::f32);
  return value[op].vi;
}

void Interpreter::store(Op *op, intptr_t v) {
  value[op] = Value { .vi = v };
}

void Interpreter::store(Op *op, float v) {
  value[op] = Value { .vf = v };
}

// The registers are in fact 64-bit.
#define EXEC_BINARY(Ty, sign) \
  case Ty::id: \
    store(op, (intptr_t) ((eval(op->DEF(0)) sign eval(op->DEF(1))) & 0xffffffff)); \
    break

#define EXEC_BINARY_L(Ty, sign) \
  case Ty::id: \
    store(op, (intptr_t) ((eval(op->DEF(0)) sign eval(op->DEF(1))))); \
    break

#define EXEC_BINARY_F(Ty, sign) \
  case Ty::id: \
    store(op, evalf(op->DEF(0)) sign evalf(op->DEF(1))); \
    break

#define EXEC_UNARY(Ty, sign) \
  case Ty::id: \
    store(op, (intptr_t) (sign eval(op->DEF()))); \
    break

// Defined in Pass.cpp
namespace sys {
  bool isExtern(const std::string &name);
}

void Interpreter::exec(Op *op) {
  switch (op->getID()) {
  case IntOp::id:
    store(op, (intptr_t) V(op));
    break;
  case FloatOp::id:
    store(op, F(op));
    break;
  EXEC_BINARY(AddIOp, +);
  EXEC_BINARY(SubIOp, -);
  EXEC_BINARY(MulIOp, *);
  EXEC_BINARY(DivIOp, /);
  EXEC_BINARY(ModIOp, %);
  EXEC_BINARY(EqOp, ==);
  EXEC_BINARY(NeOp, !=);
  EXEC_BINARY(LtOp, <);
  EXEC_BINARY(LeOp, <=);
  EXEC_BINARY(AndIOp, &);
  EXEC_BINARY(OrIOp, |);
  EXEC_BINARY(XorIOp, ^);
  EXEC_BINARY(LShiftOp, <<);
  EXEC_BINARY(RShiftOp, >>);
  
  EXEC_BINARY_L(AddLOp, +);
  EXEC_BINARY_L(MulLOp, *);
  EXEC_BINARY_L(RShiftLOp, *);

  EXEC_BINARY_F(AddFOp, +);
  EXEC_BINARY_F(SubFOp, -);
  EXEC_BINARY_F(MulFOp, *);
  EXEC_BINARY_F(DivFOp, /);

  EXEC_UNARY(NotOp, !);
  EXEC_UNARY(MinusOp, -);
  case CallOp::id: {
    const auto &name = NAME(op);
    if (isExtern(name))
      value[op] = applyExtern(name);
    else {
      auto operands = op->getOperands();
      std::vector<Value> args;
      args.reserve(operands.size());
      for (auto operand : operands)
        args.push_back(value[operand.defining]);

      SemanticScope scope(*this);
      execf(fnMap[name]->getRegion(), args);
    }
    break;
  }
  case AllocaOp::id: {
    bool fp = op->has<FPAttr>();
    void *space = alloca(SIZE(op));
    store(op, (intptr_t) space);
    break;
  }
  case PhiOp::id: {
    const auto &ops = op->getOperands();
    const auto &attrs = op->getAttrs();
    bool success = false;
    for (int i = 0; i < ops.size(); i++) {
      if (FROM(attrs[i]) == prev) {
        value[op] = value[ops[i].defining];
        success = true;
        break;
      }
    }
    if (!success)
      sys_unreachable("undef phi: coming from " << bbmap[prev]);
    break;
  }
  default:
    sys_unreachable("unknown op type: " << ip);
  }
}

Interpreter::Value Interpreter::applyExtern(const std::string &name) {
  if (name == "getint") {
    int x; inbuf >> x;
    return Value { .vi = x };
  }
  sys_unreachable("unknown extern function: " << name);
}

Interpreter::Value Interpreter::execf(Region *region, const std::vector<Value> &args) {
  auto entry = region->getFirstBlock();
  ip = entry->getFirstOp();
  while (!isa<ReturnOp>(ip)) {
    switch (ip->getID()) {
    case GotoOp::id: {
      auto dest = TARGET(ip);
      prev = ip->getParent();
      ip = dest->getFirstOp();
      break;
    }
    case BranchOp::id: {
      auto def = ip->DEF(0);
      auto dest = eval(def) ? TARGET(ip) : ELSE(ip);
      prev = ip->getParent();
      ip = dest->getFirstOp();
      break;
    }
    default:
      exec(ip);
      ip = ip->nextOp();
    }
  }
  // Now `ip` is a ReturnOp.
  if (ip->getOperandCount()) {
    auto def = ip->DEF(0);
    return value[def];
  }
  return Value();
}

void Interpreter::run(std::istream &input) {
  inbuf << input.rdbuf();
  auto exit = execf(fnMap["main"]->getRegion(), {});
  retcode = exit.vi;
}
