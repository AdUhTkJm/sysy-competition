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
    if (isa<GlobalOp>(op)) {
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

    if (isa<FuncOp>(op)) {
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
  if (!value.count(op))
    sys_unreachable("undefined op" << op);
  if (op->getResultType() == sys::Value::f32)
    sys_unreachable("op of float type: " << op);
  return value[op].vi;
}

float Interpreter::evalf(Op *op) {
  if (!value.count(op))
    sys_unreachable("undefined op" << op);
  if (op->getResultType() != sys::Value::f32)
    sys_unreachable("op of non-float type: " << op);
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

#define EXEC_BINARY_FCOMP(Ty, sign) \
  case Ty::id: \
    store(op, (intptr_t) (evalf(op->DEF(0)) sign evalf(op->DEF(1)))); \
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
  case I2FOp::id:
    store(op, (float) eval(op->DEF()));
    break;
  case F2IOp::id:
    store(op, (intptr_t) evalf(op->DEF()));
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
  
  EXEC_BINARY_L(AddLOp, +);
  EXEC_BINARY_L(MulLOp, *);
  EXEC_BINARY_L(RShiftLOp, >>);

  EXEC_BINARY_F(AddFOp, +);
  EXEC_BINARY_F(SubFOp, -);
  EXEC_BINARY_F(MulFOp, *);
  EXEC_BINARY_F(DivFOp, /);
  EXEC_BINARY_FCOMP(EqFOp, ==);
  EXEC_BINARY_FCOMP(LeFOp, <=);
  EXEC_BINARY_FCOMP(LtFOp, <);
  EXEC_BINARY_FCOMP(NeFOp, !=);

  EXEC_UNARY(NotOp, !);
  EXEC_UNARY(SetNotZeroOp, !!);
  EXEC_UNARY(MinusOp, -);
  case RShiftOp::id: {
    auto x = eval(op->DEF(0));
    auto y = eval(op->DEF(1));
    if (x & 0x80000000)
      // This is a negative 32-bit integer.
      store(op, (intptr_t) ((int) x >> y));
    else
      store(op, x >> y);
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
  case GetGlobalOp::id: {
    const auto &name = NAME(op);
    if (!globalMap.count(name))
      sys_unreachable("unknown global: " << name);

    value[op] = globalMap[name];
    break;
  }
  case LoadOp::id: {
    size_t size = SIZE(op);
    bool fp = op->getResultType() == sys::Value::f32;
    intptr_t addr = eval(op->DEF());
    if (fp)
      store(op, *(float*) addr);
    else if (size == 4)
      store(op, (intptr_t) *(int*) addr);
    else if (size == 8)
      store(op, *(intptr_t*) addr);
    else
      assert(false);
    break;
  }
  case StoreOp::id: {
    size_t size = SIZE(op);
    intptr_t addr = eval(op->DEF(1));
    Op *def = op->DEF(0);
    bool fp = def->getResultType() == sys::Value::f32;
    if (fp)
      *(float*) addr = evalf(def);
    else if (size == 4)
      *(int*) addr = eval(def);
    else if (size == 8)
      *(intptr_t*) addr = eval(def);
    else
      assert(false);
    break;
  }
  default:
    sys_unreachable("unknown op type: " << ip);
  }
}

Interpreter::Value Interpreter::applyExtern(const std::string &name, const std::vector<Value> &args) {
  if (name == "getint") {
    int x; inbuf >> x;
    return Value { .vi = x };
  }
  if (name == "getch") {
    char x = inbuf.get();
    return Value { .vi = x };
  }
  if (name == "getfloat") {
    float x; inbuf >> x;
    return Value { .vf = x };
  }
  if (name == "putint") {
    intptr_t v = args[0].vi;
    // Direct cast of `(int) v` is implementation-defined.
    outbuf << (int) (unsigned) v;
    return Value();
  }
  if (name == "putch") {
    outbuf << (char) args[0].vi;
    return Value();
  }
  if (name == "_sysy_starttime" || name == "_sysy_stoptime")
    return Value();
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
    // Note that we need the stack space to live long enough,
    // till we exit this interpreted function.
    case AllocaOp::id: {
      void *space = alloca(SIZE(ip));
      store(ip, (intptr_t) space);
      ip = ip->nextOp();
      break;
    }
    case CallOp::id: {
      const auto &name = NAME(ip);
      auto operands = ip->getOperands();
      std::vector<Value> args;
      args.reserve(operands.size());
      for (auto operand : operands)
        args.push_back(value[operand.defining]);

      if (isExtern(name)) {
        value[ip] = applyExtern(name, args);
        ip = ip->nextOp();
      }
      else {
        Op *call = ip;
        Value v;
        {
          SemanticScope scope(*this);
          v = execf(fnMap[name]->getRegion(), args);
        }
        value[call] = v;
        ip = call->nextOp();
      }
      break;
    }
    case GetArgOp::id: {
      value[ip] = args[V(ip)];
      ip = ip->nextOp();
      break;
    }
    default:
      exec(ip);
      ip = ip->nextOp();
      break;
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
