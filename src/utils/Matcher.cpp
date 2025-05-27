#include "Matcher.h"
#include "../codegen/Attrs.h"
#include <iostream>

using namespace sys;

#define MATCH_BINARY(opcode, Ty) \
  if (opname == opcode && isa<Ty>(op)) { \
    return matchExpr(list->elements[1], op->getOperand(0).defining) && \
          matchExpr(list->elements[2], op->getOperand(1).defining); \
  }

#define MATCH_UNARY(opcode, Ty) \
  if (opname == opcode && isa<Ty>(op)) { \
    return matchExpr(list->elements[1], op->getOperand(0).defining); \
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

#define EVAL_BINARY_F_OPERAND(opcode, op) \
  if (opname == "!" opcode) { \
    float a = evalFExpr(list->elements[1]); \
    float b = evalFExpr(list->elements[2]); \
    return a op b; \
  }

#define EVAL_UNARY_F_OPERAND(opcode, op) \
  if (opname == "!" opcode) { \
    float a = evalFExpr(list->elements[1]); \
    return op a; \
  }

#define EVAL_BINARY_F(opcode, op) \
  if (opname == "?" opcode) { \
    float a = evalFExpr(list->elements[1]); \
    float b = evalFExpr(list->elements[2]); \
    return a op b; \
  }

#define EVAL_UNARY_F(opcode, op) \
  if (opname == "?" opcode) { \
    float a = evalFExpr(list->elements[1]); \
    return op a; \
  }

#define EVAL_UNARY_I_OPERAND(opcode, op) \
  if (opname == "?" opcode) { \
    int a = evalExpr(list->elements[1]); \
    return op a; \
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

Rule::Rule(const char *text): text(text) {
  pattern = parse();
}

Rule::~Rule() {
  release(pattern);
}

void Rule::release(Expr *expr) {
  if (auto list = dyn_cast<List>(expr)) {
    for (auto elem : list->elements)
      release(elem);
  }
  delete expr;
}

void Rule::dump(std::ostream &os) {
  dump(pattern, os);
  os << "\n===== binding starts =====\n";
  for (auto [k, v] : binding) {
    os << k << " = ";
    v->dump(os);
  }
  os << "\n===== binding ends =====\n";
}

void Rule::dump(Expr *expr, std::ostream &os) {
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

std::string_view Rule::nextToken() {
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

Expr *Rule::parse() {
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

bool Rule::matchExpr(Expr *expr, Op* op) {
  if (auto* atom = dyn_cast<Atom>(expr)) {
    std::string_view var = atom->value;

    // This is a float literal.
    if (var[0] == '*') {
      if (!isa<FloatOp>(op))
        return false;

      if (std::isdigit(var[1]) || var[1] == '-') {
        std::string str(var.substr(1));
        if (std::stof(str) != F(op))
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
  MATCH_BINARY("feq", EqFOp);
  MATCH_BINARY("fne", NeFOp);
  MATCH_BINARY("fle", LeFOp);
  MATCH_BINARY("flt", LtFOp);
  MATCH_BINARY("add", AddIOp);
  MATCH_BINARY("sub", SubIOp);
  MATCH_BINARY("mul", MulIOp);
  MATCH_BINARY("div", DivIOp);
  MATCH_BINARY("mod", ModIOp);
  MATCH_BINARY("and", AndIOp);
  MATCH_BINARY("or", OrIOp);
  MATCH_BINARY("xor", XorIOp);
  MATCH_BINARY("shl", LShiftOp);
  MATCH_BINARY("shr", RShiftOp);
  MATCH_BINARY("shrl", RShiftLOp);
  MATCH_BINARY("addl", AddLOp);
  MATCH_BINARY("mull", MulLOp);
  MATCH_BINARY("fadd", AddFOp);
  MATCH_BINARY("fsub", SubFOp);
  MATCH_BINARY("fmul", MulFOp);
  MATCH_BINARY("fdiv", DivFOp);
  MATCH_BINARY("store", StoreOp);

  MATCH_UNARY("not", NotOp);
  MATCH_UNARY("snz", SetNotZeroOp);
  MATCH_UNARY("minus", MinusOp);
  MATCH_UNARY("fminus", MinusFOp);
  MATCH_UNARY("br", BranchOp);
  MATCH_UNARY("f2i", F2IOp);
  MATCH_UNARY("i2f", I2FOp);
  MATCH_UNARY("load", LoadOp);

  return false;
}

int Rule::evalExpr(Expr *expr) {
  if (auto atom = dyn_cast<Atom>(expr)) {
    if (std::isdigit(atom->value[0]) || atom->value[0] == '-') {
      std::string str(atom->value);
      return std::stoi(str);
    }

    if (atom->value[0] == '\'') {
      auto lint = binding[atom->value];
      return V(lint);
    }
  }

  auto list = dyn_cast<List>(expr);

  assert(list && !list->elements.empty());

  auto head = dyn_cast<Atom>(list->elements[0]);
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
  EVAL_BINARY("and", &);
  EVAL_BINARY("or", |);

  EVAL_BINARY_F_OPERAND("feq", ==);
  EVAL_BINARY_F_OPERAND("fne", !=);
  EVAL_BINARY_F_OPERAND("fle", <=);
  EVAL_BINARY_F_OPERAND("fge", >=);
  EVAL_BINARY_F_OPERAND("flt", <);
  EVAL_BINARY_F_OPERAND("fgt", >);

  EVAL_UNARY("not", !);

  EVAL_UNARY_F_OPERAND("cvt", (int));

  if (opname == "!only-if") {
    int a = evalExpr(list->elements[1]);
    if (!a)
      failed = true;
    return 0;
  }

  std::cerr << "unknown opname: " << opname << "\n";
  assert(false);
}

float Rule::evalFExpr(Expr *expr) {
  if (auto atom = dyn_cast<Atom>(expr)) {
    if (std::isdigit(atom->value[1]) || atom->value[1] == '-') {
      std::string str(atom->value.substr(1));
      return std::stof(str);
    }

    if (atom->value[0] == '*') {
      auto lint = binding[atom->value];
      return F(lint);
    }
  }

  auto list = dyn_cast<List>(expr);

  assert(list && !list->elements.empty());

  auto head = dyn_cast<Atom>(list->elements[0]);
  std::string_view opname = head->value;

  EVAL_BINARY_F("add", +);
  EVAL_BINARY_F("sub", -);
  EVAL_BINARY_F("mul", *);
  EVAL_BINARY_F("div", /);
  
  EVAL_UNARY_I_OPERAND("cvt", (float));

  std::cerr << "unknown opname: " << opname << "\n";
  assert(false);
}

Op *Rule::buildExpr(Expr *expr) {
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

  auto list = dyn_cast<List>(expr);

  assert(list && !list->elements.empty());

  auto head = dyn_cast<Atom>(list->elements[0]);
  std::string_view opname = head->value;

  if (opname[0] == '!') {
    int result = evalExpr(expr);
    if (opname == "!only-if" && !failed)
      return buildExpr(list->elements[2]);

    return builder.create<IntOp>({ new IntAttr(result) });
  }

  if (opname[0] == '?') {
    float result = evalFExpr(expr);

    return builder.create<FloatOp>({ new FloatAttr(result) });
  }

  BUILD_BINARY("add", AddIOp);
  BUILD_BINARY("sub", SubIOp);
  BUILD_BINARY("mul", MulIOp);
  BUILD_BINARY("div", DivIOp);
  BUILD_BINARY("addl", AddLOp);
  BUILD_BINARY("mull", MulLOp);
  BUILD_BINARY("fadd", AddFOp);
  BUILD_BINARY("fsub", SubFOp);
  BUILD_BINARY("fmul", MulFOp);
  BUILD_BINARY("fdiv", DivFOp);
  BUILD_BINARY("mod", ModIOp);
  BUILD_BINARY("and", AndIOp);
  BUILD_BINARY("or", OrIOp);
  BUILD_BINARY("eq", EqOp);
  BUILD_BINARY("ne", NeOp);
  BUILD_BINARY("le", LeOp);
  BUILD_BINARY("lt", LtOp);

  BUILD_UNARY("minus", MinusOp);
  BUILD_UNARY("fminus", MinusFOp);
  BUILD_UNARY("not", NotOp);
  BUILD_UNARY("snz", SetNotZeroOp);

  if (opname == "gt") {
    Value a = buildExpr(list->elements[1]);
    Value b = buildExpr(list->elements[2]);
    return builder.create<LtOp>({ b, a });
  }

  if (opname == "ge") {
    Value a = buildExpr(list->elements[1]);
    Value b = buildExpr(list->elements[2]);
    return builder.create<LeOp>({ b, a });
  }

  std::cerr << "unknown opname: " << opname << "\n";
  assert(false);
}

bool Rule::match(Op *op, const std::map<std::string, Op*> &external) {
  loc = 0;
  failed = false;
  binding.clear();
  externalStrs.clear();
  
  for (auto [k, v] : external) {
    externalStrs.push_back(k);
    binding[externalStrs.back()] = v;
  }

  return matchExpr(pattern, op);
}

Op *Rule::extract(const std::string &name) {
  if (!binding.count(name)) {
    std::cerr << "querying unknown name: " << name << "\n";
    dump();
    assert(false);
  }
  return binding[name];
}

bool Rule::rewrite(Op *op) {
  loc = 0;
  failed = false;
  binding.clear();
  
  auto list = dyn_cast<List>(pattern);
  assert(dyn_cast<Atom>(list->elements[0])->value == "change");
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
