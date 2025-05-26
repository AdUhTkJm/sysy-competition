#include <iostream>
#include <fstream>

#include "ArmPasses.h"

using namespace sys;
using namespace sys::arm;

#define DUMP_RD(lreg) << lreg(RD(op)) << ", "
#define DUMP_I << ", " << V(op)

#define TERNARY(Ty, name, lreg) \
  case Ty::id: \
    os << name << ' ' DUMP_RD(lreg) << lreg(RS(op)) << ", " << lreg(RS2(op)) << ", " << lreg(RS3(op)) << "\n"; \
    break

#define BINARY_BASE(Ty, name, lreg, X) \
  case Ty::id: \
    os << name << ' ' X << lreg(RS(op)) << ", " << lreg(RS2(op)) << "\n"; \
    break

#define BINARY(Ty, name, lreg) BINARY_BASE(Ty, name, lreg, DUMP_RD(lreg))

#define UNARY_BASE(Ty, name, lreg, X, Y) \
  case Ty::id: \
    os << name << ' ' X << lreg(RS(op)) Y << "\n"; \
    break  

#define UNARY_I_NO_RD(Ty, name, lreg) UNARY_BASE(Ty, name, lreg,, DUMP_I)
#define UNARY_I(Ty, name, lreg) UNARY_BASE(Ty, name, lreg, DUMP_RD(lreg), DUMP_I)
#define UNARY(Ty, name, lreg) UNARY_BASE(Ty, name, lreg, DUMP_RD(lreg),)

#define JMP(Ty, name) \
  case Ty::id: \
    os << name << " bb" << bbcnt(TARGET(op)) << "\n"; \
    break

#define JMP_UNARY(Ty, name) \
  case Ty::id: \
    os << name << ' ' << wreg(RS(op)) << ", bb" << bbcnt(TARGET(op)) << "\n"; \
    break

#define TERNARY_W(Ty, name) TERNARY(Ty, name, wreg)
#define TERNARY_X(Ty, name) TERNARY(Ty, name, xreg)
#define BINARY_W(Ty, name) BINARY(Ty, name, wreg)
#define BINARY_X(Ty, name) BINARY(Ty, name, xreg)
#define BINARY_F(Ty, name) BINARY(Ty, name, freg)
#define BINARY_NO_RD_W(Ty, name) BINARY_BASE(Ty, name, wreg,)
#define BINARY_NO_RD_X(Ty, name) BINARY_BASE(Ty, name, xreg,)
#define BINARY_NO_RD_F(Ty, name) BINARY_BASE(Ty, name, freg,)
#define UNARY_I_NO_RD_W(Ty, name) UNARY_I_NO_RD(Ty, name, wreg)
#define UNARY_I_NO_RD_X(Ty, name) UNARY_I_NO_RD(Ty, name, xreg)
#define UNARY_I_W(Ty, name) UNARY_I(Ty, name, wreg)
#define UNARY_I_X(Ty, name) UNARY_I(Ty, name, xreg)
#define UNARY_W(Ty, name) UNARY(Ty, name, wreg)
#define UNARY_X(Ty, name) UNARY(Ty, name, xreg)
#define UNARY_F(Ty, name) UNARY(Ty, name, freg)

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

std::string freg(Reg reg) {
  auto name = showReg(reg);
  name[0] = 's';
  return name;
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

  BINARY_F(FaddOp, "fadd");
  BINARY_F(FsubOp, "fsub");
  BINARY_F(FmulOp, "fmul");
  BINARY_F(FdivOp, "fdiv");

  BINARY_NO_RD_W(CmpOp, "cmp");
  BINARY_NO_RD_W(TstOp, "tst");
  BINARY_NO_RD_F(FcmpOp, "tst");

  BINARY_X(AddXOp, "add");
  BINARY_X(MulXOp, "mul");

  UNARY_I_W(AddWIOp, "add");

  UNARY_I_NO_RD_W(CmpIOp, "cmp");

  UNARY_I_X(AddXIOp, "add");

  UNARY_X(MovROp, "mov");
  UNARY_W(NegOp, "neg");

  UNARY_F(FnegOp, "fneg");
  UNARY_F(FmovOp, "fmov");

  JMP(BOp, "b");
  JMP(BneOp, "bne");
  JMP(BeqOp, "beq");
  JMP(BltOp, "blt");
  JMP(BleOp, "ble");
  JMP(BgtOp, "bgt");
  JMP(BgeOp, "bge");

  JMP_UNARY(CbzOp, "cbz");
  JMP_UNARY(CbnzOp, "cbnz");

  case FcmpZOp::id:
    os << "fcmp " << freg(RD(op)) << ", #0.0\n";
    break;
  case AdrOp::id:
    os << "ldr " << xreg(RD(op)) << ", =" << NAME(op) << "\n";
    break;
  case FmovWOp::id:
    os << "fmov " << freg(RD(op)) << ", " << wreg(RS(op)) << "\n";
    break;
  case BlOp::id:
    os << "bl " << NAME(op) << "\n";
    break;
  case StrXOp::id:
    os << "str " << xreg(RS(op)) << ", [" << xreg(RS2(op)) << ", #" << V(op) << "]\n";
    break;
  case StrWOp::id:
    os << "str " << wreg(RS(op)) << ", [" << xreg(RS2(op)) << ", #" << V(op) << "]\n";
    break;
  case StrFOp::id:
    os << "str " << freg(RS(op)) << ", [" << xreg(RS2(op)) << ", #" << V(op) << "]\n";
    break;
  case LdrXOp::id:
    os << "ldr " << xreg(RD(op)) << ", [" << xreg(RS(op)) << ", #" << V(op) << "]\n";
    break;
  case LdrWOp::id:
    os << "ldr " << wreg(RD(op)) << ", [" << xreg(RS(op)) << ", #" << V(op) << "]\n";
    break;
  case LdrFOp::id:
    os << "ldr " << freg(RD(op)) << ", [" << xreg(RS(op)) << ", #" << V(op) << "]\n";
    break;
  case CsetLtOp::id:
    os << "cset " << wreg(RD(op)) << ", lt\n";
    break;
  case CsetLeOp::id:
    os << "cset " << wreg(RD(op)) << ", le\n";
    break;
  case CsetNeOp::id:
    os << "cset " << wreg(RD(op)) << ", ne\n";
    break;
  case CsetEqOp::id:
    os << "cset " << wreg(RD(op)) << ", eq\n";
    break;
  case RetOp::id:
    os << "ret \n";
    break;
  case ScvtfOp::id:
    os << "scvtf " << freg(RD(op)) << ", " << wreg(RS(op)) << "\n";
    break;
  case FcvtzsOp::id:
    os << "fcvtzs " << wreg(RD(op)) << ", " << freg(RS(op)) << "\n";
    break;
  case MovIOp::id:
    os << "mov " << wreg(RD(op)) << ", " << V(op) << "\n";
    break;
  case MovkOp::id:
    os << "movk " << wreg(RD(op)) << ", " << V(op) << ", lsl " << LSL(op) << "\n";
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

    auto region = func->getRegion();
    dumpBody(region, os);
    os << "\n\n";
  }

  auto globals = collectGlobals();
  if (globals.empty())
    return;

  os << "\n\n.section .data\n.balign 16\n";
  std::vector<Op*> bss;
  for (auto global : globals) {
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
    if (auto fArr = global->find<FloatArrayAttr>()) {
      if (fArr->allZero) {
        bss.push_back(global);
        continue;
      }

      os << NAME(global) << ":\n";
      os << "  .float " << fArr->vf[0];
      for (size_t i = 1; i < size / 4; i++)
        os << ", " << fArr->vf[i];
      os << "\n";
    }
  }

  if (bss.size()) {
    os << "\n\n.section .bss\n.balign 16\n";
    for (auto global : bss) {
      os << NAME(global) << ":\n  .skip " << SIZE(global) << "\n";
    }
  }
}

void Dump::run() {
  if (out.size() != 0) {
    std::ofstream ofs(out);
    dump(ofs);
  } else
    dump(std::cout);
}
