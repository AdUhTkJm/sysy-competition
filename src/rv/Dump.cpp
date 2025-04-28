#include <iostream>
#include <fstream>
#include <ostream>
#include <sstream>

#include "RvOps.h"
#include "RvPasses.h"
#include "RvAttrs.h"
#include "../codegen/Attrs.h"

using namespace sys;
using namespace sys::rv;

static std::map<BasicBlock*, int> bbcount;
static int id = 0;

int getCount(BasicBlock *bb) {
  if (!bbcount.count(bb))
    bbcount[bb] = id++;
  return bbcount[bb];
}

std::ostream &operator<<(std::ostream &os, Reg reg) {
  return os << showReg(reg);
}

void dumpOp(Op *op, std::ostream &os) {
  std::string name;
  auto &opname = op->getName();

  // Skip the initial "rv."
  assert(opname[0] == 'r' && opname[1] == 'v' && opname[2] == '.');
  name.reserve(opname.size() - 3);
  for (int i = 3; i < opname.size(); i++)
    name.push_back(opname[i]);

  // Vary opname based on the size attribute.
  // TODO.
  if (isa<sys::rv::StoreOp>(op)) {
    auto size = op->getAttr<SizeAttr>()->value;
    switch (size) {
    case 8:
      name = "sd";
      break;
    case 4:
      name = "sw";
      break;
    default:
      assert(false);
    }
    // Dump as `sw a0, 4(a1)`
    auto rs = op->getAttr<RsAttr>()->reg;
    auto rs2 = op->getAttr<Rs2Attr>()->reg;
    auto offset = op->getAttr<IntAttr>()->value;
    os << name << " " << rs << ", " << offset << "(" << rs2 << ")\n";
    return;
  }

  if (isa<sys::rv::LoadOp>(op)) {
    auto size = op->getAttr<SizeAttr>()->value;
    switch (size) {
    case 8:
      name = "ld";
      break;
    case 4:
      name = "lw";
      break;
    default:
      assert(false);
    }
    auto rd = op->getAttr<RdAttr>()->reg;
    auto rs = op->getAttr<RsAttr>()->reg;
    auto offset = op->getAttr<IntAttr>()->value;
    os << name << " " << rd << ", " << offset << "(" << rs << ")\n";
    return;
  }

  std::stringstream ss;
  ss << name << " ";
  
  if (op->hasAttr<RdAttr>()) {
    auto rd = op->getAttr<RdAttr>()->reg;
    ss << rd << ", ";
  }

  if (op->hasAttr<RsAttr>()) {
    auto rs = op->getAttr<RsAttr>()->reg;
    ss << rs << ", ";
  }

  if (op->hasAttr<Rs2Attr>()) {
    auto rs2 = op->getAttr<Rs2Attr>()->reg;
    ss << rs2 << ", ";
  }

  if (op->hasAttr<IntAttr>()) {
    auto vi = op->getAttr<IntAttr>()->value;
    ss << vi << ", ";
  }

  if (op->hasAttr<TargetAttr>()) {
    auto bb = op->getAttr<TargetAttr>()->bb;
    ss << "bb" << getCount(bb) << ", ";
  }

  if (op->hasAttr<NameAttr>()) {
    auto name = op->getAttr<NameAttr>()->name;
    ss << name << ", ";
  }

  auto str = ss.str();
  // Remove the end ", "
  if (str.size() > 2 && str[str.size() - 2] == ',') {
    str.pop_back();
    str.pop_back();
  }
  os << str << "\n";
}

void Dump::dump(std::ostream &os) {
  os << ".global main\n";

  auto funcs = module->findAll<FuncOp>();

  for (auto func : funcs) {
    os << func->getAttr<NameAttr>()->name << ":\n";
    for (auto bb : func->getRegion()->getBlocks()) {
      os << "bb" << getCount(bb) << ":\n";

      for (auto op : bb->getOps()) {
        os << "  ";
        dumpOp(op, os);
      }
    }
  }

  auto globals = module->findAll<GlobalOp>();

  if (!globals.empty())
    os << ".data\n";
  for (auto global : globals) {
    os << global->getAttr<NameAttr>()->name << ":\n";
    // It must have been 4 bytes long for each element.
    auto size = global->getAttr<SizeAttr>()->value;
    assert(size >= 1);

    if (auto intArr = global->findAttr<IntArrayAttr>()) {
      os << "  .word " << intArr->vi[0];
      for (size_t i = 1; i < size / 4; i++)
        os << ", " << intArr->vi[i];
      os << "\n";
    }
    // .float for FloatArray
  }
}

void Dump::run() {
  if (out.size() != 0) {
    std::ofstream ofs(out);
    dump(ofs);
  } else dump(std::cout);
}
