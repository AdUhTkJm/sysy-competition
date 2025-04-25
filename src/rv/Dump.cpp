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

void dumpOp(Op *op, std::ostream &os) {
  std::string name;
  auto &opname = op->getName();

  // Skip the initial "rv."
  name.reserve(opname.size() - 3);
  for (int i = 3; i < opname.size(); i++)
    name.push_back(opname[i]);

  // Vary opname based on the size attribute.
  // TODO.
  if (isa<sys::rv::StoreOp>(op)) {
    assert(false);
    return;
  }

  if (isa<sys::rv::LoadOp>(op)) {
    assert(false);
    return;
  }

  std::stringstream ss;
  ss << name << " ";
  
  if (op->hasAttr<RdAttr>()) {
    auto rd = op->getAttr<RdAttr>()->reg;
    ss << showReg(rd) << ", ";
  }

  if (op->hasAttr<RsAttr>()) {
    auto rs = op->getAttr<RsAttr>()->reg;
    ss << showReg(rs) << ", ";
  }

  if (op->hasAttr<Rs2Attr>()) {
    auto rs2 = op->getAttr<Rs2Attr>()->reg;
    ss << showReg(rs2) << ", ";
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
}

void Dump::run() {
  if (out.size() != 0) {
    std::ofstream ofs(out);
    dump(ofs);
  } else dump(std::cout);
}
