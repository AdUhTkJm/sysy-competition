#include "Passes.h"

using namespace sys;

std::map<std::string, int> GVN::stats() {
  return {
    { "eliminated-ops", elim }
  };
}

bool GVN::Expr::operator<(const Expr &other) const {
  if (id != other.id)
    return id < other.id;
  if (operands.size() != other.operands.size())
    return operands.size() < other.operands.size();
  for (size_t i = 0; i < operands.size(); i++) {
    if (operands[i] != other.operands[i])
      return operands[i] < other.operands[i];
  }
  if (vi != other.vi)
    return vi < other.vi;
  if (vf != other.vf)
    return vf < other.vf;
  return name < other.name;
}

#define ALLOW(Ty) || isa<Ty>(op)
bool allowed(Op *op) {
  return
    (isa<CallOp>(op) && !op->has<ImpureAttr>())
    ALLOW(AddIOp)
    ALLOW(SubIOp)
    ALLOW(MulIOp)
    ALLOW(DivIOp)
    ALLOW(ModIOp)
    ALLOW(AddFOp)
    ALLOW(SubFOp)
    ALLOW(MulFOp)
    ALLOW(DivFOp)
    ALLOW(ModFOp)
    ALLOW(AddLOp)
    ALLOW(MulLOp)
    ALLOW(EqOp)
    ALLOW(NeOp)
    ALLOW(LtOp)
    ALLOW(LeOp)
    ALLOW(EqFOp)
    ALLOW(NeFOp)
    ALLOW(LtFOp)
    ALLOW(LeFOp)
    ALLOW(IntOp)
    ALLOW(FloatOp)
    ALLOW(MinusOp)
    ALLOW(MinusFOp)
    ALLOW(F2IOp)
    ALLOW(I2FOp)
    ALLOW(NotOp)
    ALLOW(LShiftOp)
    ALLOW(RShiftOp)
    ALLOW(RShiftLOp)
    ALLOW(MulshOp)
    ALLOW(MuluhOp)
    ALLOW(SetNotZeroOp)
    ALLOW(GetGlobalOp)
  ;
}

void GVN::dvnt(BasicBlock *bb, Domtree &domtree) {
  SemanticScope scope(*this);

  auto phis = bb->getPhis();
  for (auto phi : phis) {
    // Empty phi is not allowed.
    assert(phi->getOperandCount() > 0);

    Value common = phi->getOperand(0);
    if (symbols.count(common.defining)) {
      // This phi is meaningless if all of its operands share the same value.
      bool meaningless = true;
      for (auto v : phi->getOperands()) {
        if (!(symbols.count(v.defining) && symbols[v.defining] == symbols[common.defining])) {
          meaningless = false;
          break;
        }
      }

      if (meaningless) {
        elim++;
        phi->replaceAllUsesWith(common.defining);
        phi->erase();
        continue;
      }
    }

    // This phi is a new value. Add it to symbols.
    symbols[phi] = num++;
  }

  auto ops = bb->getOps();
  for (auto op : ops) {
    // Processed beforehand.
    if (isa<PhiOp>(op))
      continue;

    if (!allowed(op)) {
      symbols[op] = num++;
      continue;
    }
    
    Expr key { .id = op->opid };
    for (auto operand : op->getOperands()) {
      auto def = operand.defining;
      if (!symbols.count(def)) {
        std::cerr << "cannot find def:\n  " << def;
        std::cerr << "demanding op:\n  " << op;
        assert(false);
      }
      key.operands.push_back(symbols[def]);
    }

    // Canonicalize for commutative Ops.
    if (isa<AddIOp>(op) || isa<AddFOp>(op) || isa<AddLOp>(op) ||
        isa<MulIOp>(op) || isa<MulFOp>(op) || isa<MulLOp>(op))
      std::sort(key.operands.begin(), key.operands.end());

    if (auto attr = op->find<IntAttr>())
      key.vi = attr->value;
    if (auto attr = op->find<FloatAttr>())
      key.vf = attr->value;
    if (auto attr = op->find<NameAttr>())
      key.name = attr->name;

    if (exprNum.count(key)) {
      assert(numOp.count(exprNum[key]));
      elim++;
      op->replaceAllUsesWith(numOp[exprNum[key]]);
      op->erase();
    } else {
      symbols[op] = exprNum[key] = num;
      numOp[num] = op;
      num++;
    }
  }

  for (auto succ : domtree[bb])
    dvnt(succ, domtree);
}

// See https://www.cs.tufts.edu/~nr/cs257/archive/keith-cooper/value-numbering.pdf,
// "Value Numbering", Briggs, 1997
// Refer to figure 4.
void GVN::runImpl(Region *region) {
  region->updateDoms();

  // Construct a dominator tree.
  std::map<BasicBlock*, std::vector<BasicBlock*>> domtree;
  for (auto bb : region->getBlocks()) {
    if (bb->getIdom())
      domtree[bb->getIdom()].push_back(bb);
  }

  dvnt(region->getFirstBlock(), domtree);
}

void GVN::run() {
  auto funcs = collectFuncs();
  for (auto func : funcs)
    runImpl(func->getRegion());

  // Tidy up remaining phis after gvn.
  runRewriter([&](PhiOp *op) {
    // Discard trivial phis.
    if (op->getOperands().size() == 1) {
      auto def = op->getOperand().defining;
      op->replaceAllUsesWith(def);
      op->erase();
      return true;
    }
    return false;
  });
}
