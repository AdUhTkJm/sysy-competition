#include "ArmMatcher.h"
#include "ArmOps.h"
#include "../codegen/Attrs.h"
#include <iostream>

using namespace sys;
using namespace sys::arm;

#define MATCH_BINARY(opcode, Ty) \
  if (opname == opcode && isa<Ty>(op)) { \
    return matchExpr(list->elements[1], op->getOperand(0).defining) && \
           matchExpr(list->elements[2], op->getOperand(1).defining); \
  }

#define MATCH_UNARY(opcode, Ty) \
  if (opname == opcode && isa<Ty>(op)) { \
    return matchExpr(list->elements[1], op->getOperand(0).defining); \
  }

#define MATCH_UNARY_IMM(opcode, Ty) \
  if (opname == opcode && isa<Ty>(op)) { \
    if (!matchExpr(list->elements[1], op->getOperand(0).defining)) \
      return false;\
    int imm = V(op); \
    auto var = cast<Atom>(list->elements[2])->value; \
    assert(var[0] == '#'); \
    if (imms.count(var)) \
      return imms[var] == imm; \
    imms[var] = imm; \
    return true; \
  }

#define MATCH_IMM(opcode, Ty) \
  if (opname == opcode && isa<Ty>(op)) { \
    int imm = V(op); \
    auto var = cast<Atom>(list->elements[1])->value; \
    assert(var[0] == '#'); \
    if (imms.count(var)) \
      return imms[var] == imm; \
    imms[var] = imm; \
    return true; \
  }

#define MATCH_BRANCH(opcode, Ty) \
  if (opname == opcode && isa<Ty>(op)) { \
    if (!matchExpr(list->elements[1], op->getOperand().defining)) \
      return false; \
    auto target = TARGET(op); \
    auto var = cast<Atom>(list->elements[2])->value; \
    assert(var[0] == '>'); \
    if (blockBinding.count(var)) \
      return blockBinding[var] == target; \
    blockBinding[var] = target; \
    auto ifnot = ELSE(op); \
    var = cast<Atom>(list->elements[3])->value; \
    assert(var[0] == '?'); \
    if (blockBinding.count(var)) \
      return blockBinding[var] == ifnot; \
    blockBinding[var] = ifnot; \
    return true; \
  }

#define EVAL_BINARY(opcode, op) \
  if (opname == "!" opcode) { \
    int a = evalExpr(list->elements[1]); \
    int b = evalExpr(list->elements[2]); \
    return a op b; \
  }

#define EVAL_UNARY(opcode, op) \
  if (opname == "!" opcode) { \
    int a = evalExpr(list->elements[1]); \
    return op a; \
  }

#define BUILD_TERNARY(opcode, Ty) \
  if (opname == opcode) { \
    Value a = buildExpr(list->elements[1]); \
    Value b = buildExpr(list->elements[2]); \
    Value c = buildExpr(list->elements[3]); \
    return builder.create<Ty>({ a, b, c }); \
  }

#define BUILD_BINARY(opcode, Ty) \
  if (opname == opcode) { \
    Value a = buildExpr(list->elements[1]); \
    Value b = buildExpr(list->elements[2]); \
    return builder.create<Ty>({ a, b }); \
  }

#define BUILD_UNARY(opcode, Ty) \
  if (opname == opcode) { \
    Value a = buildExpr(list->elements[1]); \
    return builder.create<Ty>({ a }); \
  }

#define BUILD_UNARY_IMM(opcode, Ty) \
  if (opname == opcode) { \
    Value a = buildExpr(list->elements[1]); \
    int b = evalExpr(list->elements[2]); \
    return builder.create<Ty>({ a }, { new IntAttr(b) }); \
  }

#define BUILD_IMM(opcode, Ty) \
  if (opname == opcode) { \
    int b = evalExpr(list->elements[1]); \
    return builder.create<Ty>({ new IntAttr(b) }); \
  }

#define BUILD_BRANCH(opcode, Ty) \
  if (opname == opcode) { \
    Value arg = buildExpr(list->elements[1]); \
    auto bb1 = cast<Atom>(list->elements[2])->value; \
    auto bb2 = cast<Atom>(list->elements[3])->value; \
    BasicBlock *target = blockBinding[bb1]; \
    BasicBlock *ifnot = blockBinding[bb2]; \
    return builder.create<Ty>({ arg }, { new TargetAttr(target), new ElseAttr(ifnot) }); \
  }

ArmRule::ArmRule(const char *text): text(text) {
  pattern = parse();
}

void ArmRule::dump(std::ostream &os) {
  dump(pattern, os);
  os << "\n===== binding starts =====\n";
  for (auto [k, v] : binding) {
    os << k << " = ";
    v->dump(os);
  }
  os << "\n===== binding ends =====\n";
}

void ArmRule::dump(Expr *expr, std::ostream &os) {
  if (auto atom = dyn_cast<Atom>(expr)) {
    os << atom->value;
    return;
  }
  auto list = dyn_cast<List>(expr);
  os << "(";
  dump(list->elements[0], os);
  for (size_t i = 1; i < list->elements.size(); i++) {
    os << " ";
    dump(list->elements[i], os);
  }
  os << ")";
}

std::string_view ArmRule::nextToken() {
  while (loc < text.size() && std::isspace(text[loc]))
    ++loc;
  
  if (loc >= text.size())
    return "";

  if (text[loc] == '(' || text[loc] == ')')
    return text.substr(loc++, 1);

  int start = loc;
  while (loc < text.size() && !std::isspace(text[loc]) && text[loc] != '(' && text[loc] != ')')
    ++loc;

  return text.substr(start, loc - start);
}

Expr *ArmRule::parse() {
  std::string_view tok = nextToken();

  if (tok == "(") {
    auto list = new List;
    for (;;) {
      std::string_view peek = text.substr(loc, 1);
      if (peek == ")") {
        nextToken();
        break;
      }
      list->elements.push_back(parse());
    }
    return list;
  }

  return new Atom(tok);
}

bool ArmRule::matchExprForLower(Expr *expr, Op* op) {
  if (auto* atom = dyn_cast<Atom>(expr)) {
    std::string_view var = atom->value;

    if (var[0] == '>') {
      // This denotes the target.
      if (blockBinding.count(var))
        return blockBinding[var] == TARGET(op);

      blockBinding[var] = TARGET(op);
      return true;
    }

    if (var[0] == '?') {
      // This denotes the "else" branch.
      if (blockBinding.count(var))
        return blockBinding[var] == ELSE(op);

      blockBinding[var] = ELSE(op);
      return true;
    }

    if (var[0] == '*') {
      // This denotes a floating point constant.
      if (std::isdigit(var[0]) || var[0] == '-') {
        std::string str(var);
        if (strtof(str.c_str(), nullptr) != F(op))
          return false;
      }
  
      if (binding.count(var))
        return F(binding[var]) == F(op);
  
      binding[var] = op;
      return true;
    }

    // A normal binding.
    if (var[0] != '\'' && !(std::isdigit(var[0]) || var[0] == '-')) {
      if (binding.count(var))
        return binding[var] == op;

      binding[var] = op;
      return true;
    }

    // This denotes a int-constant.
    if (!isa<IntOp>(op)) {
      return false;
    }

    // This is a int literal.
    if (std::isdigit(var[0]) || var[0] == '-') {
      std::string str(var);
      if (std::stoi(str) != V(op))
        return false;
    }

    if (binding.count(var))
      return V(binding[var]) == V(op);

    binding[var] = op;
    return true;
  }

  List *list = dyn_cast<List>(expr);
  if (!list)
    return false;

  assert(!list->elements.empty());
  Atom *head = dyn_cast<Atom>(list->elements[0]);
  if (!head)
    return false;

  std::string_view opname = head->value;

  MATCH_BINARY("eq", EqOp);
  MATCH_BINARY("ne", NeOp);
  MATCH_BINARY("le", LeOp);
  MATCH_BINARY("lt", LtOp);
  MATCH_BINARY("eqf", EqFOp);
  MATCH_BINARY("nef", NeFOp);
  MATCH_BINARY("lef", LeFOp);
  MATCH_BINARY("ltf", LtFOp);
  MATCH_BINARY("add", AddIOp);
  MATCH_BINARY("sub", SubIOp);
  MATCH_BINARY("mul", MulIOp);
  MATCH_BINARY("div", DivIOp);
  MATCH_BINARY("mod", ModIOp);
  MATCH_BINARY("and", AndIOp);
  MATCH_BINARY("or", OrIOp);
  MATCH_BINARY("xor", XorIOp);
  MATCH_BINARY("shl", LShiftImmOp);
  MATCH_BINARY("shr", RShiftImmOp);
  MATCH_BINARY("shrl", RShiftImmLOp);
  MATCH_BINARY("addl", AddLOp);
  MATCH_BINARY("mull", MulLOp);
  MATCH_BINARY("addf", AddFOp);
  MATCH_BINARY("subf", SubFOp);
  MATCH_BINARY("mulf", MulFOp);
  MATCH_BINARY("divf", DivFOp);

  MATCH_UNARY("not", NotOp);
  MATCH_UNARY("snz", SetNotZeroOp);
  MATCH_UNARY("minus", MinusOp);

  MATCH_BRANCH("br", BranchOp);

  if (opname == "j" && isa<GotoOp>(op)) {
    auto target = TARGET(op);
    auto var = cast<Atom>(list->elements[1])->value;
    assert(var[0] == '>');

    if (blockBinding.count(var))
      return blockBinding[var] == target;

    blockBinding[var] = target;
    return true;
  }

  return false;
}

// This is matching against ArmOps.
bool ArmRule::matchExpr(Expr *expr, Op* op) {
  if (auto* atom = dyn_cast<Atom>(expr)) {
    std::string_view var = atom->value;

    if (var[0] == '>') {
      // This denotes the target.
      if (blockBinding.count(var))
        return blockBinding[var] == TARGET(op);

      blockBinding[var] = TARGET(op);
      return true;
    }

    if (var[0] == '?') {
      // This denotes the "else" branch.
      if (blockBinding.count(var))
        return blockBinding[var] == ELSE(op);

      blockBinding[var] = ELSE(op);
      return true;
    }

    // A normal binding.
    if (var[0] != '\'' && !(std::isdigit(var[0]) || var[0] == '-')) {
      if (binding.count(var))
        return binding[var] == op;

      binding[var] = op;
      return true;
    }
  }

  List *list = dyn_cast<List>(expr);
  if (!list)
    return false;

  assert(!list->elements.empty());
  Atom *head = dyn_cast<Atom>(list->elements[0]);
  if (!head)
    return false;

  std::string_view opname = head->value;

  MATCH_BINARY("mla", MlaOp);

  MATCH_BINARY("addw", AddWOp);
  MATCH_BINARY("addx", AddXOp);
  MATCH_BINARY("subw", SubWOp);
  MATCH_BINARY("rsbw", RsbWOp);
  MATCH_BINARY("mulw", MulWOp);
  MATCH_BINARY("mulx", MulXOp);
  MATCH_BINARY("sdivw", SdivWOp);
  MATCH_BINARY("sdivx", SdivXOp);
  MATCH_BINARY("and", AndOp);
  MATCH_BINARY("or", OrOp);
  MATCH_BINARY("eor", EorOp);
  MATCH_BINARY("tst", TstOp);
  MATCH_BINARY("cmp", CmpOp);

  MATCH_UNARY_IMM("addwi", AddWIOp);
  MATCH_UNARY_IMM("subwi", SubWIOp);
  MATCH_UNARY_IMM("ldrw", LdrWOp);
  MATCH_UNARY_IMM("ldrf", LdrFOp);
  MATCH_UNARY_IMM("ldrx", LdrXOp);
  MATCH_UNARY_IMM("lsli", LslIOp);
  MATCH_UNARY_IMM("asrwi", AsrWIOp);
  MATCH_UNARY_IMM("asrxi", AsrXIOp);
  MATCH_UNARY_IMM("cmpi", CmpIOp);
  MATCH_UNARY_IMM("andi", AndIOp);
  MATCH_UNARY_IMM("ori", OrIOp);
  MATCH_UNARY_IMM("eori", EorIOp);

  MATCH_IMM("mov", MovIOp);

  MATCH_UNARY("neg", NegOp);
  MATCH_UNARY("csetne", CsetNeOp);
  MATCH_UNARY("csetlt", CsetLtOp);
  MATCH_UNARY("csetle", CsetLeOp);
  MATCH_UNARY("cseteq", CsetEqOp);

  MATCH_BRANCH("cbz", CbzOp);
  MATCH_BRANCH("cbnz", CbnzOp);
  MATCH_BRANCH("beq", BeqOp);
  MATCH_BRANCH("bne", BneOp);
  MATCH_BRANCH("blt", BltOp);
  MATCH_BRANCH("bgt", BgtOp);
  MATCH_BRANCH("ble", BleOp);

  if (opname == "j" && isa<GotoOp>(op)) {
    auto target = TARGET(op);
    auto var = cast<Atom>(list->elements[1])->value;
    assert(var[0] == '>');

    if (blockBinding.count(var))
      return blockBinding[var] == target;

    blockBinding[var] = target;
    return true;
  }

  return false;
}

int ArmRule::evalExpr(Expr *expr) {
  if (auto atom = dyn_cast<Atom>(expr)) {
    if (std::isdigit(atom->value[0]) || atom->value[0] == '-') {
      std::string str(atom->value);
      return std::stoi(str);
    }

    if (atom->value[0] == '\'') {
      auto lint = cast<IntOp>(binding[atom->value]);
      return V(lint);
    }

    if (atom->value[0] == '#') {
      assert(imms.count(atom->value));
      return imms[atom->value];
    }
    assert(false);
  }

  auto list = cast<List>(expr);
  assert(!list->elements.empty());

  auto head = cast<Atom>(list->elements[0]);
  std::string_view opname = head->value;

  EVAL_BINARY("add", +);
  EVAL_BINARY("sub", -);
  EVAL_BINARY("mul", *);
  EVAL_BINARY("div", /);
  EVAL_BINARY("mod", %);
  EVAL_BINARY("gt", >);
  EVAL_BINARY("lt", <);
  EVAL_BINARY("ge", >=);
  EVAL_BINARY("le", <=);
  EVAL_BINARY("eq", ==);
  EVAL_BINARY("ne", !=);
  EVAL_BINARY("and", &&);
  EVAL_BINARY("or", ||);
  EVAL_BINARY("bitand", &);
  EVAL_BINARY("bitor", |);
  EVAL_BINARY("xor", ^);

  EVAL_UNARY("minus", -);
  EVAL_UNARY("not", !);

  if (opname == "!inbit") {
    int bitlen = evalExpr(list->elements[1]);
    int value = evalExpr(list->elements[2]);
    return (value < (1 << bitlen)) && (value >= -(1 << bitlen));
  }

  if (opname == "!only-if") {
    int a = evalExpr(list->elements[1]);
    if (!a)
      failed = true;
    return 0;
  }

  std::cerr << "unknown opname: " << opname << "\n";
  assert(false);
}

Op *ArmRule::buildExpr(Expr *expr) {
  if (auto atom = dyn_cast<Atom>(expr)) {
    // This is an integer literal. Evaluate it.
    if (std::isdigit(atom->value[0]) || atom->value[0] == '-' || atom->value[0] == '\'') {
      int result = evalExpr(expr);
      return builder.create<IntOp>({ new IntAttr(result) });
    }

    if (!binding.count(atom->value)) {
      std::cerr << "unbound variable: " << atom->value << "\n";
      assert(false);
    }
    return binding[atom->value];
  }

  auto list = cast<List>(expr);
  assert(!list->elements.empty());

  auto head = cast<Atom>(list->elements[0]);
  std::string_view opname = head->value;

  if (opname[0] == '!') {
    int result = evalExpr(expr);
    if (opname == "!only-if" && !failed)
      return buildExpr(list->elements[2]);

    return builder.create<IntOp>({ new IntAttr(result) });
  }

  BUILD_TERNARY("mla", MlaOp);

  BUILD_BINARY("addw", AddWOp);
  BUILD_BINARY("addx", AddXOp);
  BUILD_BINARY("subw", SubWOp);
  BUILD_BINARY("rsbw", RsbWOp);
  BUILD_BINARY("mulw", MulWOp);
  BUILD_BINARY("mulx", MulXOp);
  BUILD_BINARY("sdivw", SdivWOp);
  BUILD_BINARY("sdivx", SdivXOp);
  BUILD_BINARY("and", AndOp);
  BUILD_BINARY("or", OrOp);
  BUILD_BINARY("eor", EorOp);
  BUILD_BINARY("tst", TstOp);
  BUILD_BINARY("cmp", CmpOp);

  BUILD_UNARY_IMM("addwi", AddWIOp);
  BUILD_UNARY_IMM("subwi", SubWIOp);
  BUILD_UNARY_IMM("ldrw", LdrWOp);
  BUILD_UNARY_IMM("ldrf", LdrFOp);
  BUILD_UNARY_IMM("ldrx", LdrXOp);
  BUILD_UNARY_IMM("lsli", LslIOp);
  BUILD_UNARY_IMM("cmpi", CmpIOp);
  BUILD_UNARY_IMM("andi", AndIOp);
  BUILD_UNARY_IMM("ori", OrIOp);
  BUILD_UNARY_IMM("eori", EorIOp);

  BUILD_IMM("mov", MovIOp);

  BUILD_UNARY("neg", NegOp);
  BUILD_UNARY("csetne", CsetNeOp);
  BUILD_UNARY("csetlt", CsetLtOp);
  BUILD_UNARY("csetle", CsetLeOp);
  BUILD_UNARY("cseteq", CsetEqOp);

  BUILD_BRANCH("cbz", CbzOp);
  BUILD_BRANCH("cbnz", CbnzOp);
  BUILD_BRANCH("beq", BeqOp);
  BUILD_BRANCH("bne", BneOp);
  BUILD_BRANCH("blt", BltOp);
  BUILD_BRANCH("bgt", BgtOp);
  BUILD_BRANCH("ble", BleOp);

  if (opname == "b") {
    auto bb = cast<Atom>(list->elements[1])->value;
    BasicBlock *target = blockBinding[bb];

    return builder.create<BOp>({ new TargetAttr(target) });
  }

  std::cerr << "unknown opname: " << opname << "\n";
  assert(false);
}

bool ArmRule::rewriteForLower(Op *op) {
  loc = 0;
  failed = false;
  binding.clear();
  
  auto list = cast<List>(pattern);
  assert(cast<Atom>(list->elements[0])->value == "change");
  auto matcher = list->elements[1];
  auto rewriter = list->elements[2];

  if (!matchExprForLower(matcher, op))
    return false;

  builder.setBeforeOp(op);
  Op *opnew = buildExpr(rewriter);
  if (!opnew || failed)
    return false;

  op->replaceAllUsesWith(opnew);
  op->erase();
  return true;
}

// Nearly identical as above, just the "match" condition has changed.
bool ArmRule::rewrite(Op *op) {
  loc = 0;
  failed = false;
  binding.clear();
  
  auto list = cast<List>(pattern);
  assert(cast<Atom>(list->elements[0])->value == "change");
  auto matcher = list->elements[1];
  auto rewriter = list->elements[2];

  if (!matchExpr(matcher, op))
    return false;

  builder.setBeforeOp(op);
  Op *opnew = buildExpr(rewriter);
  if (!opnew || failed)
    return false;

  op->replaceAllUsesWith(opnew);
  op->erase();
  return true;
}
