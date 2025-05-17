#include "Exec.h"
#include "../codegen/Attrs.h"
#include <cstring>

using namespace sys;
using namespace sys::exec;

#define sys_unreachable(x) \
  do { std::cerr << x; assert(false); } while (0)

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
        globalMap[name] = Value { .vp = vp };
      }
      if (auto fpArr = op->find<FloatArrayAttr>()) {
        float *vfp = new float[size];
        memcpy(vfp, fpArr->vf, size * 4);
        globalMap[name] = Value { .vfp = vfp };
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
  // Seems not UB? Not sure.
  for (auto [_, ptr] : globalMap)
    delete[] ptr.vp;
}

void Interpreter::exec(Op *op) {
  switch (op->getID()) {
  case IntOp::id:
    // Do nothing.
    break;
  default:
    sys_unreachable("unknown op type: " << ip);
  }
}

int Interpreter::eval(Op *op) {
  assert(op->getResultType() != sys::Value::f32);
  return value[op].vi;
}

float Interpreter::evalf(Op *op) {
  assert(op->getResultType() == sys::Value::f32);
  return value[op].vi;
}

int *Interpreter::evalp(Op *op) {
  assert(op->getResultType() == sys::Value::i64);
  return value[op].vp;
}

float *Interpreter::evalfp(Op *op) {
  assert(op->getResultType() == sys::Value::i64);
  return value[op].vfp;
}

Interpreter::Value Interpreter::execf(Region *region) {
  auto entry = region->getBlocks();
  while (!isa<ReturnOp>(ip)) {
    switch (ip->getID()) {
    case GotoOp::id: {
      auto dest = TARGET(ip);
      ip = dest->getFirstOp();
      break;
    }
    case BranchOp::id: {
      auto def = ip->DEF(0);
      auto dest = eval(def) ? TARGET(ip) : ELSE(ip);
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
  execf(fnMap["main"]->getRegion());
}
