#include "ArmMatcher.h"
#include "ArmOps.h"
#include "../codegen/Attrs.h"
#include <iostream>

using namespace sys;
using namespace sys::arm;

#define MATCH_TERNARY(opcode, Ty) \
  if (opname == opcode && isa<Ty>(op)) { \
    return matchExpr(list->elements[1], op->getOperand(0).defining) && \
           matchExpr(list->elements[2], op->getOperand(1).defining) && \
           matchExpr(list->elements[3], op->getOperand(2).defining); \
  }

#define MATCH_BINARY(opcode, Ty) \
  if (opname == opcode && isa<Ty>(op)) { \
    return matchExpr(list->elements[1], op->getOperand(0).defining) && \
           matchExpr(list->elements[2], op->getOperand(1).defining); \
  }

#define MATCH_UNARY(opcode, Ty) \
  if (opname == opcode && isa<Ty>(op)) { \
    return matchExpr(list->elements[1], op->getOperand(0).defining); \
  }

#define MATCH_BINARY_IMM(opcode, Ty) \
  if (opname == opcode && isa<Ty>(op)) { \
    if (!matchExpr(list->elements[1], op->getOperand(0).defining) || \
        !matchExpr(list->elements[2], op->getOperand(1).defining)) \
      return false;\
    int imm = V(op); \
    auto var = cast<Atom>(list->elements[3])->value; \
    assert(var[0] == '#'); \
    if (imms.count(var)) \
      return imms[var] == imm; \
    imms[var] = imm; \
    return true; \
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
    assert(var[0] == '>'); \
    if (blockBinding.count(var)) \
      return blockBinding[var] == ifnot; \
    blockBinding[var] = ifnot; \
    return true; \
  }

#define MATCH_BRANCH_BINARY(opcode, Ty) \
  if (opname == opcode && isa<Ty>(op)) { \
    if (!matchExpr(list->elements[1], op->getOperand(0).defining)) \
      return false; \
    if (!matchExpr(list->elements[2], op->getOperand(1).defining)) \
      return false; \
    auto target = TARGET(op); \
    auto var = cast<Atom>(list->elements[3])->value; \
    assert(var[0] == '>'); \
    if (blockBinding.count(var)) \
      return blockBinding[var] == target; \
    blockBinding[var] = target; \
    auto ifnot = ELSE(op); \
    var = cast<Atom>(list->elements[4])->value; \
    assert(var[0] == '>'); \
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

#define BUILD_BINARY_IMM(opcode, Ty) \
  if (opname == opcode) { \
    Value a = buildExpr(list->elements[1]); \
    Value b = buildExpr(list->elements[2]); \
    int c = evalExpr(list->elements[3]); \
    return builder.create<Ty>({ a, b }, { new IntAttr(c) }); \
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

#define BUILD_BRANCH_BINARY(opcode, Ty) \
  if (opname == opcode) { \
    Value arg1 = buildExpr(list->elements[1]); \
    Value arg2 = buildExpr(list->elements[2]); \
    auto bb1 = cast<Atom>(list->elements[3])->value; \
    auto bb2 = cast<Atom>(list->elements[4])->value; \
    BasicBlock *target = blockBinding[bb1]; \
    BasicBlock *ifnot = blockBinding[bb2]; \
    return builder.create<Ty>({ arg1, arg2 }, { new TargetAttr(target), new ElseAttr(ifnot) }); \
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

// This is matching against ArmOps.
bool ArmRule::matchExpr(Expr *expr, Op* op) {
  if (auto* atom = dyn_cast<Atom>(expr)) {
    std::string_view var = atom->value;

    // A normal binding.
    if (binding.count(var))
      return binding[var] == op;

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

  MATCH_TERNARY("mla", MlaOp);

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
  MATCH_BINARY("csetne", CsetNeOp);
  MATCH_BINARY("csetlt", CsetLtOp);
  MATCH_BINARY("csetle", CsetLeOp);
  MATCH_BINARY("cseteq", CsetEqOp);

  MATCH_BINARY_IMM("strw", StrWOp);
  MATCH_BINARY_IMM("strf", StrFOp);
  MATCH_BINARY_IMM("strx", StrXOp);

  MATCH_UNARY_IMM("addwi", AddWIOp);
  MATCH_UNARY_IMM("addxi", AddXIOp);
  MATCH_UNARY_IMM("subwi", SubWIOp);
  MATCH_UNARY_IMM("ldrw", LdrWOp);
  MATCH_UNARY_IMM("ldrf", LdrFOp);
  MATCH_UNARY_IMM("ldrx", LdrXOp);
  MATCH_UNARY_IMM("lslwi", LslWIOp);
  MATCH_UNARY_IMM("lslxi", LslXIOp);
  MATCH_UNARY_IMM("rslwi", LsrWIOp);
  MATCH_UNARY_IMM("lsrxi", LsrXIOp);
  MATCH_UNARY_IMM("asrwi", AsrWIOp);
  MATCH_UNARY_IMM("asrxi", AsrXIOp);
  MATCH_UNARY_IMM("andi", AndIOp);
  MATCH_UNARY_IMM("ori", OrIOp);
  MATCH_UNARY_IMM("eori", EorIOp);

  MATCH_IMM("mov", MovIOp);

  MATCH_UNARY("neg", NegOp);

  MATCH_BRANCH("cbz", CbzOp);
  MATCH_BRANCH("cbnz", CbnzOp);
  MATCH_BRANCH_BINARY("beq", BeqOp);
  MATCH_BRANCH_BINARY("bne", BneOp);
  MATCH_BRANCH_BINARY("blt", BltOp);
  MATCH_BRANCH_BINARY("bgt", BgtOp);
  MATCH_BRANCH_BINARY("ble", BleOp);
  MATCH_BRANCH_BINARY("bge", BgeOp);

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
  BUILD_BINARY("csetne", CsetNeOp);
  BUILD_BINARY("csetlt", CsetLtOp);
  BUILD_BINARY("csetle", CsetLeOp);
  BUILD_BINARY("cseteq", CsetEqOp);

  BUILD_BINARY_IMM("strw", StrWOp);
  BUILD_BINARY_IMM("strf", StrFOp);
  BUILD_BINARY_IMM("strx", StrXOp);

  BUILD_UNARY_IMM("addwi", AddWIOp);
  BUILD_UNARY_IMM("addxi", AddXIOp);
  BUILD_UNARY_IMM("subwi", SubWIOp);
  BUILD_UNARY_IMM("ldrw", LdrWOp);
  BUILD_UNARY_IMM("ldrf", LdrFOp);
  BUILD_UNARY_IMM("ldrx", LdrXOp);
  BUILD_UNARY_IMM("lslwi", LslWIOp);
  BUILD_UNARY_IMM("lslxi", LslXIOp);
  BUILD_UNARY_IMM("asrwi", AsrWIOp);
  BUILD_UNARY_IMM("asrxi", AsrXIOp);
  BUILD_UNARY_IMM("andi", AndIOp);
  BUILD_UNARY_IMM("ori", OrIOp);
  BUILD_UNARY_IMM("eori", EorIOp);

  BUILD_IMM("mov", MovIOp);

  BUILD_UNARY("neg", NegOp);

  BUILD_BRANCH("cbz", CbzOp);
  BUILD_BRANCH("cbnz", CbnzOp);
  BUILD_BRANCH_BINARY("beq", BeqOp);
  BUILD_BRANCH_BINARY("bne", BneOp);
  BUILD_BRANCH_BINARY("blt", BltOp);
  BUILD_BRANCH_BINARY("bgt", BgtOp);
  BUILD_BRANCH_BINARY("ble", BleOp);
  BUILD_BRANCH_BINARY("bge", BgeOp);

  if (opname == "b") {
    auto bb = cast<Atom>(list->elements[1])->value;
    BasicBlock *target = blockBinding[bb];

    return builder.create<BOp>({ new TargetAttr(target) });
  }

  std::cerr << "unknown opname: " << opname << "\n";
  assert(false);
}

// Nearly identical as above, just the "match" condition has changed.
bool ArmRule::rewrite(Op *op) {
  loc = 0;
  failed = false;
  binding.clear();
  blockBinding.clear();
  imms.clear();
  
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
