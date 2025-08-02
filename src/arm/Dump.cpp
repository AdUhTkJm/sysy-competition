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
    os << name << " .Lbb" << bbcnt(TARGET(op)) << "\n"; \
    break

#define JMP_UNARY(Ty, name) \
  case Ty::id: \
    os << name << ' ' << wreg(RS(op)) << ", .Lbb" << bbcnt(TARGET(op)) << "\n"; \
    break
    
#define JMP_BINARY(Ty, name) \
  case Ty::id: \
    os << "cmp " << wreg(RS(op)) << ", " << wreg(RS2(op)) << "\n  "; \
    os << name << " .Lbb" << bbcnt(TARGET(op)) << "\n"; \
    break

#define TERNARY_W(Ty, name) TERNARY(Ty, name, wreg)
#define TERNARY_X(Ty, name) TERNARY(Ty, name, xreg)
#define TERNARY_F(Ty, name) TERNARY(Ty, name, freg)
#define TERNARY_V(Ty, name) TERNARY(Ty, name, vreg)
#define BINARY_W(Ty, name) BINARY(Ty, name, wreg)
#define BINARY_X(Ty, name) BINARY(Ty, name, xreg)
#define BINARY_F(Ty, name) BINARY(Ty, name, freg)
#define BINARY_V(Ty, name) BINARY(Ty, name, vreg)
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

std::string dreg(Reg reg) {
  auto name = showReg(reg);
  name[0] = 'd';
  return name;
}

std::string vreg(Reg reg) {
  auto name = showReg(reg);
  return name + ".4s"; // 4 signed integers
}

}

void Dump::dumpOp(Op *op, std::ostream &os) {
  switch (op->opid) {
  TERNARY_W(MsubWOp, "msub");
  TERNARY_W(MaddWOp, "madd");
  
  TERNARY_X(MsubXOp, "msub");
  TERNARY_X(MaddXOp, "madd");

  TERNARY_F(FmaddOp, "fmadd");
  TERNARY_F(FmsubOp, "fmsub");

  BINARY_W(AddWOp, "add");
  BINARY_W(SubWOp, "sub");
  BINARY_W(MulWOp, "mul");
  BINARY_W(SdivWOp, "sdiv");
  BINARY_W(AsrWOp, "asr");
  BINARY_W(LslWOp, "lslv");
  BINARY_W(LsrWOp, "lsr");

  BINARY_F(FaddOp, "fadd");
  BINARY_F(FsubOp, "fsub");
  BINARY_F(FmulOp, "fmul");
  BINARY_F(FdivOp, "fdiv");

  BINARY_X(AddXOp, "add");
  BINARY_X(SubXOp, "sub");
  BINARY_X(MulXOp, "mul");
  BINARY_X(SdivXOp, "sdiv");
  BINARY_X(AsrXOp, "asr");
  BINARY_X(LslXOp, "lslv");
  BINARY_X(LsrXOp, "lsr");
  BINARY_X(AndOp, "and");
  BINARY_X(OrOp, "orr");
  BINARY_X(EorOp, "eor");

  BINARY_V(AddVOp, "add");
  BINARY_V(MulVOp, "mul");
  BINARY_V(MlaVOp, "mla");

  UNARY_I_W(AddWIOp, "add");
  UNARY_I_W(LslWIOp, "lsl");
  UNARY_I_W(LsrWIOp, "lsr");
  UNARY_I_W(AsrWIOp, "asr");

  UNARY_I_X(AddXIOp, "add");
  UNARY_I_X(LslXIOp, "lsl");
  UNARY_I_X(LsrXIOp, "lsr");
  UNARY_I_X(AsrXIOp, "asr");
  UNARY_I_X(AndIOp, "and");

  UNARY_X(MovROp, "mov");
  UNARY_W(NegOp, "neg");

  UNARY_F(FnegOp, "fneg");
  UNARY_F(FmovOp, "fmov");

  JMP(BOp, "b");
  JMP_BINARY(BneOp, "bne");
  JMP_BINARY(BeqOp, "beq");
  JMP_BINARY(BltOp, "blt");
  JMP_BINARY(BleOp, "ble");
  JMP_BINARY(BgtOp, "bgt");
  JMP_BINARY(BgeOp, "bge");

  JMP_UNARY(CbzOp, "cbz");
  JMP_UNARY(CbnzOp, "cbnz");

  case AdrOp::id:
    os << "adrp " << xreg(RD(op)) << ", " << NAME(op) << "\n  ";
    os << "add " << xreg(RD(op)) << ", " << xreg(RD(op)) << ", :lo12:" << NAME(op) << "\n";
    break;
  case FmovWOp::id:
    os << "fmov " << freg(RD(op)) << ", " << wreg(RS(op)) << "\n";
    break;
  case FmovDOp::id:
    os << "fmov " << xreg(RD(op)) << ", " << dreg(RS(op)) << "\n";
    break;
  case FmovXOp::id:
    os << "fmov " << dreg(RD(op)) << ", " << xreg(RS(op)) << "\n";
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
  case StrDOp::id:
    os << "str " << dreg(RS(op)) << ", [" << xreg(RS2(op)) << ", #" << V(op) << "]\n";
    break;
  case StpDOp::id:
    os << "stp " << dreg(RS(op)) << ", " << dreg(RS2(op)) << ", [" << xreg(RS3(op)) << ", #" << V(op) << "]\n";
    break;
  case StpXOp::id:
    os << "stp " << xreg(RS(op)) << ", " << xreg(RS2(op)) << ", [" << xreg(RS3(op)) << ", #" << V(op) << "]\n";
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
  case LdrDOp::id:
    os << "ldr " << dreg(RD(op)) << ", [" << xreg(RS(op)) << ", #" << V(op) << "]\n";
    break;
  case LdpDOp::id:
    os << "ldp " << dreg(RS(op)) << ", " << dreg(RS2(op)) << ", [" << xreg(RS3(op)) << ", #" << V(op) << "]\n";
    break;
  case LdpXOp::id:
    os << "ldp " << xreg(RS(op)) << ", " << xreg(RS2(op)) << ", [" << xreg(RS3(op)) << ", #" << V(op) << "]\n";
    break;
  case LdrXPOp::id:
    os << "ldr " << xreg(RD(op)) << ", [" << xreg(RS(op)) << "], #" << V(op) << "]\n";
    break;
  case LdrWPOp::id:
    os << "ldr " << wreg(RD(op)) << ", [" << xreg(RS(op)) << "], #" << V(op) << "\n";
    break;
  case LdrFPOp::id:
    os << "ldr " << freg(RD(op)) << ", [" << xreg(RS(op)) << "], #" << V(op) << "\n";
    break;
  case StrXPOp::id:
    os << "str " << xreg(RS(op)) << ", [" << xreg(RS2(op)) << "], #" << V(op) << "\n";
    break;
  case StrWPOp::id:
    os << "str " << wreg(RS(op)) << ", [" << xreg(RS2(op)) << "], #" << V(op) << "\n";
    break;
  case StrFPOp::id:
    os << "str " << freg(RS(op)) << ", [" << xreg(RS2(op)) << "], #" << V(op) << "\n";
    break;
  case CsetLtOp::id:
    os << "cmp " << wreg(RS(op)) << ", " << wreg(RS2(op)) << "\n  ";
    os << "cset " << wreg(RD(op)) << ", lt\n";
    break;
  case CsetLeOp::id:
    os << "cmp " << wreg(RS(op)) << ", " << wreg(RS2(op)) << "\n  ";
    os << "cset " << wreg(RD(op)) << ", le\n";
    break;
  case CsetNeOp::id:
    os << "cmp " << wreg(RS(op)) << ", " << wreg(RS2(op)) << "\n  ";
    os << "cset " << wreg(RD(op)) << ", ne\n";
    break;
  case CsetEqOp::id:
    os << "cmp " << wreg(RS(op)) << ", " << wreg(RS2(op)) << "\n  ";
    os << "cset " << wreg(RD(op)) << ", eq\n";
    break;
  case CsetLtFOp::id:
    os << "fcmp " << freg(RS(op)) << ", " << freg(RS2(op)) << "\n  ";
    os << "cset " << wreg(RD(op)) << ", lt\n";
    break;
  case CsetLeFOp::id:
    os << "fcmp " << freg(RS(op)) << ", " << freg(RS2(op)) << "\n  ";
    os << "cset " << wreg(RD(op)) << ", le\n";
    break;
  case CsetNeFOp::id:
    os << "fcmp " << freg(RS(op)) << ", " << freg(RS2(op)) << "\n  ";
    os << "cset " << wreg(RD(op)) << ", ne\n";
    break;
  case CsetEqFOp::id:
    os << "fcmp " << freg(RS(op)) << ", " << freg(RS2(op)) << "\n  ";
    os << "cset " << wreg(RD(op)) << ", eq\n";
    break;
  case CsetEqFcmpZOp::id:
    os << "fcmpz " << freg(RS(op)) << "\n  ";
    os << "cset " << wreg(RD(op)) << ", eq\n";
    break;
  case CsetNeFcmpZOp::id:
    os << "fcmpz " << freg(RS(op)) << "\n  ";
    os << "cset " << wreg(RD(op)) << ", ne\n";
    break;
  case CsetNeTstOp::id:
    os << "tst " << wreg(RS(op)) << ", " << wreg(RS2(op)) << "\n  ";
    os << "cset " << wreg(RD(op)) << ", ne\n";
    break;
  case CsetEqTstOp::id:
    os << "tst " << wreg(RS(op)) << ", " << wreg(RS2(op)) << "\n  ";
    os << "cset " << wreg(RD(op)) << ", eq\n";
    break;
  case CselNeZOp::id:
    os << "cmp " << wreg(RS(op)) << ", #0\n  ";
    os << "csel " << wreg(RD(op)) << ", " << wreg(RS2(op)) << ", " << wreg(RS3(op)) << ", ne\n";
    break;
  case CselLtZOp::id:
    os << "cmp " << wreg(RS(op)) << ", #0\n  ";
    os << "csel " << wreg(RD(op)) << ", " << wreg(RS2(op)) << ", " << wreg(RS3(op)) << ", lt\n";
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
  case MovnOp::id:
    os << "movn " << wreg(RD(op)) << ", " << V(op) << "\n";
    break;
  case MovkOp::id:
    os << "movk " << wreg(RD(op)) << ", " << V(op) << ", lsl " << LSL(op) << "\n";
    break;
  case AddWLOp::id:
    os << "add " << wreg(RD(op))  << ", " << wreg(RS(op)) << ", " << wreg(RS2(op)) << ", lsl " << V(op) << "\n";
    break;
  case AddXLOp::id:
    os << "add " << xreg(RD(op))  << ", " << xreg(RS(op)) << ", " << xreg(RS2(op)) << ", lsl " << V(op) << "\n";
    break;
  case AddWROp::id:
    os << "add " << wreg(RD(op))  << ", " << wreg(RS(op)) << ", " << wreg(RS2(op)) << ", lsr " << V(op) << "\n";
    break;
  case AddWAROp::id:
    os << "add " << wreg(RD(op))  << ", " << wreg(RS(op)) << ", " << wreg(RS2(op)) << ", asr " << V(op) << "\n";
    break;
  case LdrWROp::id:
    os << "ldr " << wreg(RD(op)) << ", [" << xreg(RS(op)) << ", " << xreg(RS2(op));
    if (V(op))
      os << ", lsl " << V(op);
    os << "]\n";
    break;
  case LdrXROp::id:
    os << "ldr " << xreg(RD(op)) << ", [" << xreg(RS(op)) << ", " << xreg(RS2(op));
    if (V(op))
      os << ", lsl " << V(op);
    os << "]\n";
    break;
  case LdrFROp::id:
    os << "ldr " << freg(RD(op)) << ", [" << xreg(RS(op)) << ", " << xreg(RS2(op));
    if (V(op))
      os << ", lsl " << V(op);
    os << "]\n";
    break;
  case StrWROp::id:
    os << "str " << wreg(RS(op)) << ", [" << xreg(RS2(op)) << ", " << xreg(RS3(op));
    if (V(op))
      os << ", lsl " << V(op);
    os << "]\n";
    break;
  case StrXROp::id:
    os << "str " << xreg(RS(op)) << ", [" << xreg(RS2(op)) << ", " << xreg(RS3(op));
    if (V(op))
      os << ", lsl " << V(op);
    os << "]\n";
    break;
  case StrFROp::id:
    os << "str " << freg(RS(op)) << ", [" << xreg(RS2(op)) << ", " << xreg(RS3(op));
    if (V(op))
      os << ", lsl " << V(op);
    os << "]\n";
    break;
  case SmullOp::id:
    os << "smull " << xreg(RD(op)) << ", " << wreg(RS(op)) << ", " << wreg(RS2(op)) << "\n";
    break;
  case CnegLtZOp::id:
    os << "cmp " << wreg(RS(op)) << ", #0\n  ";
    os << "cneg " << wreg(RD(op)) << ", " << wreg(RS2(op)) << ", lt\n";
    break;
  case DupOp::id:
    os << "dup " << vreg(RD(op)) << ", " << wreg(RS(op)) << "\n";
    break;
  case St1Op::id:
    os << "st1 {" << vreg(RS(op)) << "}, [" << xreg(RS2(op)) << "]\n";
    break;
  case Ld1Op::id:
    os << "ld1 {" << vreg(RD(op)) << "}, [" << xreg(RS(op)) << "]\n";
    break;
  case JoinOp::id:
    os << "adrp x0, _lock" << NAME(op) << "\n  ";
    os << "add x0, x0, :lo12:_lock" << NAME(op) << "\n  ";
    os << "bl spinlock_wait\n";
    break;
  case CloneOp::id:
    os << "adrp x0, " << NAME(op) << "\n  ";
    os << "add x0, x0, :lo12:" << NAME(op) << "\n  ";
    os << "adrp x1, _stack" << NAME(op) << "\n  ";
    os << "add x1, x1, :lo12:_stack" << NAME(op) << "\n  ";
    os << "add x1, x1, #8192\n  ";
    os << "mov x2, #1\n  ";
    os << "adrp x3, _lock" << NAME(op) << "\n  ";
    os << "add x3, x3, :lo12:_lock" << NAME(op) << "\n  ";
    os << "str x2, [x3]\n  ";
    os << "bl instantiate_worker\n";
    break;
  case WakeOp::id:
    os << "adrp x0, _lock" << NAME(op) << "\n  ";
    os << "add x0, x0, :lo12:_lock" << NAME(op) << "\n  ";
    os << "dmb ish\n  ";
    os << "mov w1, #0\n  ";
    os << "stlr w1, [x0]\n";
    break;
  default:
    std::cerr << "unimplemented op: " << op;
    assert(false);
  }
}

void Dump::dumpBody(Region *region, std::ostream &os) {
  for (auto bb : region->getBlocks()) {
    os << ".Lbb" << bbcnt(bb) << ":\n";
    
    for (auto op : bb->getOps()) {
      os << "  ";
      dumpOp(op, os);
    }
  }
}

static void operator>>(const char *p, std::ostream &w) {
  w << p;
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

  auto clones = module->findAll<CloneOp>();
  if (clones.size()) {
#include "../rt/arm-clone.s"
    >> os;
#include "../rt/arm-join.s"
    >> os;
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
    os << "\n\n.section .bss\n";
    for (auto global : bss) {
      os << ".balign 16\n";
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
