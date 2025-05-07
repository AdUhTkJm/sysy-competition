#include "Matcher.h"
#include "../codegen/Attrs.h"
#include <iostream>

using namespace sys;

Rule::Rule(const char *text): text(text) {
  pattern = parse();
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

// This time just use `dynamic_cast`. 
// For things this simple, there's no need to get a dyn_cast, isa and cast for them.
bool Rule::matchExpr(Expr *expr, Op* op) {
  if (auto* atom = dyn_cast<Atom>(expr)) {
    std::string_view var = atom->value;

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

  if (opname == "eq" && isa<EqOp>(op)) {
    return matchExpr(list->elements[1], op->getOperand(0).defining) &&
           matchExpr(list->elements[2], op->getOperand(1).defining);
  }

  if (opname == "ne" && isa<NeOp>(op)) {
    return matchExpr(list->elements[1], op->getOperand(0).defining) &&
           matchExpr(list->elements[2], op->getOperand(1).defining);
  }

  if (opname == "le" && isa<LeOp>(op)) {
    return matchExpr(list->elements[1], op->getOperand(0).defining) &&
           matchExpr(list->elements[2], op->getOperand(1).defining);
  }

  if (opname == "lt" && isa<LtOp>(op)) {
    return matchExpr(list->elements[1], op->getOperand(0).defining) &&
           matchExpr(list->elements[2], op->getOperand(1).defining);
  }

  if (opname == "minus" && isa<MinusOp>(op)) {
    return matchExpr(list->elements[1], op->getOperand(0).defining);
  }

  if (opname == "add" && isa<AddIOp>(op)) {
    return matchExpr(list->elements[1], op->getOperand(0).defining) &&
           matchExpr(list->elements[2], op->getOperand(1).defining);
  }

  if (opname == "sub" && isa<SubIOp>(op)) {
    return matchExpr(list->elements[1], op->getOperand(0).defining) &&
           matchExpr(list->elements[2], op->getOperand(1).defining);
  }

  if (opname == "mul" && isa<MulIOp>(op)) {
    return matchExpr(list->elements[1], op->getOperand(0).defining) &&
           matchExpr(list->elements[2], op->getOperand(1).defining);
  }

  if (opname == "div" && isa<DivIOp>(op)) {
    return matchExpr(list->elements[1], op->getOperand(0).defining) &&
           matchExpr(list->elements[2], op->getOperand(1).defining);
  }

  if (opname == "mod" && isa<ModIOp>(op)) {
    return matchExpr(list->elements[1], op->getOperand(0).defining) &&
           matchExpr(list->elements[2], op->getOperand(1).defining);
  }

  if (opname == "not" && isa<NotOp>(op)) {
    return matchExpr(list->elements[1], op->getOperand(0).defining);
  }

  if (opname == "snz" && isa<SetNotZeroOp>(op)) {
    return matchExpr(list->elements[1], op->getOperand(0).defining);
  }

  return false;
}

int Rule::evalExpr(Expr *expr) {
  if (auto atom = dyn_cast<Atom>(expr)) {
    if (std::isdigit(atom->value[0]) || atom->value[0] == '-') {
      std::string str(atom->value);
      return std::stoi(str);
    }

    if (atom->value[0] == '\'') {
      auto lint = cast<IntOp>(binding[atom->value]);
      return V(lint);
    }
  }

  auto list = dyn_cast<List>(expr);

  assert(list && !list->elements.empty());

  auto head = dyn_cast<Atom>(list->elements[0]);
  std::string_view opname = head->value;

  if (opname == "!add") {
    int a = evalExpr(list->elements[1]);
    int b = evalExpr(list->elements[2]);
    return a + b;
  }

  if (opname == "!mul") {
    int a = evalExpr(list->elements[1]);
    int b = evalExpr(list->elements[2]);
    return a * b;
  }
  
  if (opname == "!sub") {
    int a = evalExpr(list->elements[1]);
    int b = evalExpr(list->elements[2]);
    return a - b;
  }

  if (opname == "!div") {
    int a = evalExpr(list->elements[1]);
    int b = evalExpr(list->elements[2]);
    return a / b;
  }

  if (opname == "!mod") {
    int a = evalExpr(list->elements[1]);
    int b = evalExpr(list->elements[2]);
    return a % b;
  }

  if (opname == "!gt") {
    int a = evalExpr(list->elements[1]);
    int b = evalExpr(list->elements[2]);
    return a > b;
  }

  if (opname == "!lt") {
    int a = evalExpr(list->elements[1]);
    int b = evalExpr(list->elements[2]);
    return a < b;
  }
  
  if (opname == "!le") {
    int a = evalExpr(list->elements[1]);
    int b = evalExpr(list->elements[2]);
    return a <= b;
  }

  if (opname == "!eq") {
    int a = evalExpr(list->elements[1]);
    int b = evalExpr(list->elements[2]);
    return a == b;
  }

  if (opname == "!ne") {
    int a = evalExpr(list->elements[1]);
    int b = evalExpr(list->elements[2]);
    return a != b;
  }
  
  if (opname == "!and") {
    int a = evalExpr(list->elements[1]);
    int b = evalExpr(list->elements[2]);
    return a && b;
  }
  
  if (opname == "!or") {
    int a = evalExpr(list->elements[1]);
    int b = evalExpr(list->elements[2]);
    return a || b;
  }

  if (opname == "!not") {
    int a = evalExpr(list->elements[1]);
    return !a;
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

  if (opname == "add") {
    Value a = buildExpr(list->elements[1]);
    Value b = buildExpr(list->elements[2]);
    return builder.create<AddIOp>({ a, b });
  }

  if (opname == "sub") {
    Value a = buildExpr(list->elements[1]);
    Value b = buildExpr(list->elements[2]);
    return builder.create<SubIOp>({ a, b });
  }

  if (opname == "mul") {
    Value a = buildExpr(list->elements[1]);
    Value b = buildExpr(list->elements[2]);
    return builder.create<MulIOp>({ a, b });
  }

  if (opname == "div") {
    Value a = buildExpr(list->elements[1]);
    Value b = buildExpr(list->elements[2]);
    return builder.create<DivIOp>({ a, b });
  }

  if (opname == "mod") {
    Value a = buildExpr(list->elements[1]);
    Value b = buildExpr(list->elements[2]);
    return builder.create<ModIOp>({ a, b });
  }

  if (opname == "and") {
    Value a = buildExpr(list->elements[1]);
    Value b = buildExpr(list->elements[2]);
    return builder.create<AndIOp>({ a, b });
  }

  if (opname == "or") {
    Value a = buildExpr(list->elements[1]);
    Value b = buildExpr(list->elements[2]);
    return builder.create<OrIOp>({ a, b });
  }

  if (opname == "eq") {
    Value a = buildExpr(list->elements[1]);
    Value b = buildExpr(list->elements[2]);
    return builder.create<EqOp>({ a, b });
  }

  if (opname == "lt") {
    Value a = buildExpr(list->elements[1]);
    Value b = buildExpr(list->elements[2]);
    return builder.create<LtOp>({ a, b });
  }

  if (opname == "le") {
    Value a = buildExpr(list->elements[1]);
    Value b = buildExpr(list->elements[2]);
    return builder.create<LeOp>({ a, b });
  }

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

  if (opname == "minus") {
    Value a = buildExpr(list->elements[1]);
    return builder.create<MinusOp>({ a });
  }

  if (opname == "not") {
    Value a = buildExpr(list->elements[1]);
    return builder.create<NotOp>({ a });
  }

  if (opname == "snz") {
    Value a = buildExpr(list->elements[1]);
    return builder.create<SetNotZeroOp>({ a });
  }

  std::cerr << "unknown opname: " << opname << "\n";
  assert(false);
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
