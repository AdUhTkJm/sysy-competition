#include "Passes.h"
#include <iostream>
#include <memory>

using namespace sys;

#define INT(op) isa<IntOp>(op)

struct Expr {
  virtual ~Expr() {}
};

struct Atom : Expr {
  std::string_view value;
  Atom(std::string_view value): value(value) {}
};

struct List : Expr {
  // Anyway, I don't want to manage my memory for this simple parser anymore.
  std::vector<std::shared_ptr<Expr>> elements;
};

struct Rule {
  std::map<std::string_view, Op*> binding;
  std::string_view text;
  std::shared_ptr<Expr> pattern;
  Builder builder;
  int loc = 0;
  bool failed = false;

  std::string_view nextToken();
  std::shared_ptr<Expr> parse();

  bool matchExpr(std::shared_ptr<Expr> expr, Op *op);
  int evalExpr(std::shared_ptr<Expr> expr);
  Op *buildExpr(std::shared_ptr<Expr> expr);

  void dump(std::shared_ptr<Expr> expr, std::ostream &os);
public:
  Rule(const char *text): text(text) { pattern = parse(); }
  bool rewrite(Op *op);

  void dump(std::ostream &os);
};

const Rule rules[] = {
  // Addition
  "(change (add x 0) x)",
  "(change (add 'a 'b) (!add 'a 'b))",
  "(change (add 'a x) (add x 'a))",
  "(change (add x (minus y)) (sub x y))",
  "(change (add (minus x) y) (sub y x))",
  "(change (add (add x 'a) 'b) (add x (!add 'a 'b)))",
  "(change (add (mul x 'a) (mul x 'b)) (mul x (!add 'a 'b)))",
  "(change (add (mul x 'a) (mul y 'a)) (mul (add x y) 'a))",
  "(change (add (div 'a x) (div 'b x)) (div (!add 'a 'b) x))",
  "(change (add (div x 'a) (div y 'a)) (div (add x y) 'a))",

  // Subtraction
  "(change (sub x 0) x)",
  "(change (sub x x) 0)",
  "(change (sub 'a 'b) (!sub 'a 'b))",
  "(change (sub (add x y) x) y)",
  "(change (sub (add x y) y) x)",
  "(change (sub (add x 'a) 'b) (add x (!sub 'a 'b)))",
  "(change (sub (sub x 'a) 'b) (sub x (!add 'a 'b)))",
  "(change (sub x (minus y)) (add x y))",
  "(change (sub (mul x 'a) (mul x 'b)) (mul x (!sub 'a 'b)))",
  "(change (sub (mul x 'a) (mul y 'a)) (mul (sub x y) 'a))",
  "(change (sub (div 'a x) (div 'b x)) (div (!sub 'a 'b) x))",
  "(change (sub (div x 'a) (div y 'a)) (div (sub x y) 'a))",

  // Multiplication
  "(change (mul x 0) 0)",
  "(change (mul x 1) x)",
  "(change (mul x -1) (minus x))",
  "(change (mul 'a 'b) (!mul 'a 'b))",
  "(change (mul 'a x) (mul x 'a))",

  // Division
  "(change (div 0 x) 0)",
  "(change (div x 1) x)",
  "(change (div x -1) (minus x))",
  "(change (div x x) 1)",
  "(change (div 'a 'b) (!div 'a 'b))",

  // Modulus
  "(change (mod x 1) 0)",
  "(change (mod x x) 0)",
  "(change (mod 0 x) 0)",
  "(change (mod 'a 'b) (!mod 'a 'b))",

  // Equality
  "(change (eq x x) 1)",
  "(change (eq 'a 'b) (!eq 'a 'b))",
  "(change (eq 'a x) (eq x 'a))",
  "(change (eq (add x 'a) 'b) (eq x (!sub 'b 'a)))",
  "(change (eq (sub x 'a) 'b) (eq x (!add 'b 'a)))",
  "(change (eq (mul x 'a) 'b) (!only-if (!eq 0 (!mod 'b 'a)) (eq x (!div 'b 'a))))",
  "(change (eq (mul x 'a) 'b) (!only-if (!ne 0 (!mod 'b 'a)) 0))",
  "(change (eq (div x 'a) 'b) (!only-if (!gt 'a 0) (and (lt x (!mul (!add 'b 1) 'a)) (gt x (!mul 'b 'a)))))",
  "(change (eq (mod x 'a) 'b) (!only-if (!le 'a 'b) 0))",

  // Less than or equal
  "(change (le x x) 1)",
  "(change (le 'a 'b) (!le 'a 'b))",
  "(change (le x 'a) (lt x (!add 'a 1)))",
  "(change (le 'a x) (lt (!sub 'a 1) x))",

  // Less than
  "(change (lt x x) 0)",
  "(change (lt 'a 'b) (!lt 'a 'b))",
  "(change (lt (add x 'a) 'b) (lt x (!sub 'b 'a)))",
  "(change (lt (sub x 'a) 'b) (lt x (!add 'b 'a)))",
  "(change (lt (mul x 'a) 'b) (!only-if (!and (!gt 'a 0) (!gt 'b 0)) (lt x (!div 'b 'a))))",
  "(change (lt (div x 'a) 'b) (!only-if (!and (!gt 'a 0) (!gt 'b 0)) (lt x (!mul 'b 'a))))",
  "(change (lt 'b (add x 'a)) (lt (!sub 'b 'a) x))",
  "(change (lt 'b (sub x 'a)) (lt (!add 'b 'a) x))",
  "(change (lt 'b (mul x 'a)) (!only-if (!and (!gt 'a 0) (!gt 'b 0)) (lt (!div 'b 'a) x)))",
  "(change (lt 'b (div x 'a)) (!only-if (!and (!gt 'a 0) (!gt 'b 0)) (le (!mul 'a (!add 'b 1)) x)))",

};

void Rule::dump(std::ostream &os) {
  dump(pattern, os);
  os << "\n===== binding starts =====\n";
  for (auto [k, v] : binding) {
    os << k << " = ";
    v->dump(os);
  }
  os << "\n===== binding ends =====\n";
}

void Rule::dump(std::shared_ptr<Expr> expr, std::ostream &os) {
  if (auto atom = dynamic_cast<Atom*>(expr.get())) {
    os << atom->value;
    return;
  }
  auto list = dynamic_cast<List*>(expr.get());
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

std::shared_ptr<Expr> Rule::parse() {
  std::string_view tok = nextToken();

  if (tok == "(") {
    auto list = std::make_shared<List>();
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

  return std::make_shared<Atom>(tok);
}

// This time just use `dynamic_cast`. 
// For things this simple, there's no need to get a dyn_cast, isa and cast for them.
bool Rule::matchExpr(std::shared_ptr<Expr> expr, Op* op) {
  if (auto* atom = dynamic_cast<Atom*>(expr.get())) {
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

  List *list = dynamic_cast<List*>(expr.get());
  if (!list)
    return false;

  assert(!list->elements.empty());
  Atom *head = dynamic_cast<Atom*>(list->elements[0].get());
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

  return false;
}

int Rule::evalExpr(std::shared_ptr<Expr> expr) {
  if (auto atom = dynamic_cast<Atom*>(expr.get())) {
    if (std::isdigit(atom->value[0]) || atom->value[0] == '-') {
      std::string str(atom->value);
      return std::stoi(str);
    }

    if (atom->value[0] == '\'') {
      auto lint = cast<IntOp>(binding[atom->value]);
      return V(lint);
    }
  }

  auto list = dynamic_cast<List*>(expr.get());

  assert(list && !list->elements.empty());

  auto head = std::dynamic_pointer_cast<Atom>(list->elements[0]);
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

  if (opname == "!only-if") {
    int a = evalExpr(list->elements[1]);
    if (!a)
      failed = true;
    return 0;
  }

  std::cerr << "unknown opname: " << opname << "\n";
  assert(false);
}

Op *Rule::buildExpr(std::shared_ptr<Expr> expr) {
  if (auto atom = dynamic_cast<Atom*>(expr.get())) {
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

  auto list = dynamic_cast<List*>(expr.get());

  assert(list && !list->elements.empty());

  auto head = std::dynamic_pointer_cast<Atom>(list->elements[0]);
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

  std::cerr << "unknown opname: " << opname << "\n";
  assert(false);
}

bool Rule::rewrite(Op *op) {
  loc = 0;
  failed = false;
  binding.clear();
  
  auto list = dynamic_cast<List*>(pattern.get());
  assert(dynamic_cast<Atom*>(list->elements[0].get())->value == "change");
  auto matcher = list->elements[1];
  auto rewriter = list->elements[2];

  if (!matchExpr(matcher, op))
    return false;

  builder.setBeforeOp(op);
  Op *opnew = buildExpr(rewriter);
  if (!opnew || failed)
    return false;

  //op->dump(std::cerr);
  //opnew->dump(std::cerr);
  op->replaceAllUsesWith(opnew);
  op->erase();
  return true;
}

std::map<std::string, int> RegularFold::stats() {
  return {
    { "folded-ops", foldedTotal }
  };
}

void RegularFold::run() {
  // foldedTotal = foldImpl();
  auto funcs = collectFuncs();
  int folded;
  do {
    folded = 0;
    for (auto func : funcs) {
      auto region = func->getRegion();

      for (auto bb : region->getBlocks()) {
        auto ops = bb->getOps();
        for (auto op : ops) {
          for (auto rule : rules)
            folded += rule.rewrite(op);
        }
      }
    }
  } while (folded);
}
