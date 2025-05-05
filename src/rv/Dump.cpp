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

  static const std::map<std::string, std::string> nameMap = {
    { "fadd", "fadd.s" },
    { "fsub", "fsub.s" },
    { "fmul", "fmul.s" },
    { "fdiv", "fdiv.s" },
    { "fcvtsw", "fcvt.s.w" },
    { "fmvwx", "fmv.w.x" },
  };

  // Skip the initial "rv."
  assert(opname[0] == 'r' && opname[1] == 'v' && opname[2] == '.');
  name.reserve(opname.size() - 3);
  for (int i = 3; i < opname.size(); i++)
    name.push_back(opname[i]);

  if (nameMap.count(name))
    name = nameMap.at(name);

  // Vary opname based on the size attribute.
  if (isa<sys::rv::StoreOp>(op)) {
    if (!isFP(op->get<RsAttr>()->reg)) {
      auto size = SIZE(op);
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
    } else {
      // A very ad-hoc, ugly hack. Since all FPs are float, we just ignore `size` (which might be 8).
      name = "fsw";
    }
    // Dump as `sw a0, 4(a1)`
    auto rs = op->get<RsAttr>()->reg;
    auto rs2 = op->get<Rs2Attr>()->reg;
    auto offset = V(op);
    os << name << " " << rs << ", " << offset << "(" << rs2 << ")\n";
    return;
  }

  if (isa<sys::rv::LoadOp>(op)) {
    auto size = SIZE(op);
    if (!isFP(op->get<RdAttr>()->reg)) {
      auto size = SIZE(op);
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
    } else {
      // A very ad-hoc, ugly hack. Since all FPs are float, we just ignore `size` (which might be 8).
      name = "flw";
    }
    auto rd = op->get<RdAttr>()->reg;
    auto rs = op->get<RsAttr>()->reg;
    auto offset = V(op);
    os << name << " " << rd << ", " << offset << "(" << rs << ")\n";
    return;
  }

  if (isa<FcvtwsRtzOp>(op)) {
    auto rd = op->get<RdAttr>()->reg;
    auto rs = op->get<RsAttr>()->reg;
    os << "fcvt.w.s " << rd << ", " << rs << ", rtz\n";
    return;
  }

  std::stringstream ss;
  ss << name << " ";
  
  if (op->has<RdAttr>()) {
    auto rd = op->get<RdAttr>()->reg;
    ss << rd << ", ";
  }

  if (op->has<RsAttr>()) {
    auto rs = op->get<RsAttr>()->reg;
    ss << rs << ", ";
  }

  if (op->has<Rs2Attr>()) {
    auto rs2 = op->get<Rs2Attr>()->reg;
    ss << rs2 << ", ";
  }

  if (op->has<IntAttr>()) {
    auto vi = V(op);
    ss << vi << ", ";
  }

  if (op->has<TargetAttr>()) {
    auto bb = TARGET(op);
    ss << "bb" << getCount(bb) << ", ";
  }

  if (op->has<NameAttr>()) {
    auto name = NAME(op);
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
    os << NAME(func) << ":\n";
    for (auto bb : func->getRegion()->getBlocks()) {
      os << "bb" << getCount(bb) << ":\n";

      for (auto op : bb->getOps()) {
        os << "  ";
        dumpOp(op, os);
      }
    }
    os << "\n\n";
  }

  auto globals = module->findAll<GlobalOp>();
  // Arrays of all zeros should be put in .bss segment.
  std::vector<GlobalOp*> bss;

  if (!globals.empty())
    os << "\n.data\n";
  for (auto global : globals) {
    // It must have been 4 bytes long for each element.
    // Here `size` is the total number of bytes.
    auto size = SIZE(global);
    assert(size >= 1);

    if (auto intArr = global->find<IntArrayAttr>()) {
      if (intArr->allZero) {
        bss.push_back(global);
        continue;
      }

      os << NAME(global) << ":\n";
      os << "  .word " << intArr->vi[0];
      for (size_t i = 1; i < size / 4; i++)
        os << ", " << intArr->vi[i];
      os << "\n";
    }
    // .float for FloatArray
  }

  if (!bss.empty())
    os << "\n.bss\n  .align 4\n";
  for (auto global : bss) {
    os << NAME(global) << ":\n  .space ";
    os << SIZE(global) << "\n";
  }
}

void Dump::run() {
  if (out.size() != 0) {
    std::ofstream ofs(out);
    dump(ofs);
  } else dump(std::cout);
}
