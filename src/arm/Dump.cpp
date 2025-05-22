#include <iostream>
#include <fstream>

#include "ArmPasses.h"

using namespace sys;
using namespace sys::arm;

#define TERNARY(Ty, name, lreg) \
  case Ty::id: \
    os << name << ' ' << lreg(RD(op)) << ", " << lreg(RS(op)) << ", " << lreg(RS2(op)) << ", " << lreg(RS3(op)) << "\n"; \
    break

#define BINARY(Ty, name, lreg) \
  case Ty::id: \
    os << name << ' ' << lreg(RD(op)) << ", " << lreg(RS(op)) << ", " << lreg(RS2(op)) << "\n"; \
    break

#define BINARY_NO_RD(Ty, name, lreg) \
  case Ty::id: \
    os << name << ' ' << lreg(RS(op)) << ", " << lreg(RS2(op)) << "\n"; \
    break

#define UNARY_I(Ty, name, lreg) \
  case Ty::id: \
    os << name << ' ' << lreg(RD(op)) << ", " << lreg(RS(op)) << ", " << V(op) << "\n"; \
    break

#define UNARY(Ty, name, lreg) \
  case Ty::id: \
    os << name << ' ' << lreg(RD(op)) << ", " << lreg(RS(op)) << "\n"; \
    break  

#define JMP(Ty, name) \
  case Ty::id: \
    os << name << " bb" << bbcnt(TARGET(op)) << "\n"; \
    break

#define JMP_UNARY(Ty, name) \
  case Ty::id: \
    os << name << ' ' << RS(op) << ", bb" << bbcnt(TARGET(op)) << "\n"; \
    break

#define TERNARY_W(Ty, name) TERNARY(Ty, name, wreg)
#define TERNARY_X(Ty, name) TERNARY(Ty, name, xreg)
#define BINARY_W(Ty, name) BINARY(Ty, name, wreg)
#define BINARY_X(Ty, name) BINARY(Ty, name, xreg)
#define BINARY_NO_RD_W(Ty, name) BINARY_NO_RD(Ty, name, wreg)
#define BINARY_NO_RD_X(Ty, name) BINARY_NO_RD(Ty, name, xreg)
#define UNARY_I_W(Ty, name) UNARY_I(Ty, name, wreg)
#define UNARY_I_X(Ty, name) UNARY_I(Ty, name, xreg)
#define UNARY_W(Ty, name) UNARY(Ty, name, wreg)
#define UNARY_X(Ty, name) UNARY(Ty, name, xreg)

namespace {

std::map<BasicBlock*, int> bbout;
int bbcount = 0;

int bbcnt(BasicBlock *bb) {
  if (!bbout.count(bb))
    bbout[bb] = bbcount++;
  return bbout[bb];
}

std::string wreg(Reg reg) {
  auto name = showReg(reg);
  name[0] = 'w';
  return name;
}

std::string xreg(Reg reg) {
  return showReg(reg);
}

}

void Dump::dumpOp(Op *op, std::ostream &os) {
  switch (op->getID()) {
  TERNARY_W(MlaOp, "mla");
  TERNARY_W(MsubWOp, "msub");

  BINARY_W(AddWOp, "add");
  BINARY_W(SubWOp, "sub");
  BINARY_W(MulWOp, "mul");
  BINARY_W(SdivWOp, "sdiv");

  BINARY_NO_RD_W(CmpOp, "cmp");

  BINARY_X(AddXOp, "add");
  BINARY_X(MulXOp, "mul");

  UNARY_I_W(AddWIOp, "add");

  UNARY_I_X(AddXIOp, "add");

  UNARY_X(MovROp, "mov");

  JMP(BOp, "b");
  JMP(BneOp, "bne");
  JMP(BeqOp, "beq");
  JMP(BltOp, "blt");
  JMP(BleOp, "ble");
  JMP(BgtOp, "bgt");
  JMP(BgeOp, "bge");

  JMP_UNARY(CbzOp, "cbz");
  JMP_UNARY(CbnzOp, "cbnz");

  case RetOp::id:
    os << "ret \n";
    break;
  case MovIOp::id:
    os << "mov " << RD(op) << ", " << V(op) << "\n";
    break;
  default:
    std::cerr << "unimplemented op: " << op;
    assert(false);
  }
}

void Dump::dumpBody(Region *region, std::ostream &os) {
  for (auto bb : region->getBlocks()) {
    os << "bb" << bbcnt(bb) << ":\n";
    
    for (auto op : bb->getOps()) {
      os << "  ";
      dumpOp(op, os);
    }
  }
}

void Dump::dump(std::ostream &os) {
  os << ".global main\n\n";

  auto funcs = collectFuncs();
  for (auto func : funcs) {
    os << NAME(func) << ":\n";
    dumpBody(func->getRegion(), os);
    os << "\n\n";
  }
}

void Dump::run() {
  if (out.size() != 0) {
    std::ofstream ofs(out);
    dump(ofs);
  } else
    dump(std::cout);
}
