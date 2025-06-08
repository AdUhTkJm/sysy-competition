#include "BvMatcher.h"
#include "../DynamicCast.h"
#include <iostream>

using namespace smt;
using namespace sys;

#define MATCH_TERNARY(opcode, Ty) \
  if (opname == opcode && bvexpr->ty == BvExpr::Ty) { \
    return matchExpr(list->elements[1], bvexpr->cond) && \
           matchExpr(list->elements[2], bvexpr->l) && \
           matchExpr(list->elements[3], bvexpr->r); \
  }

#define MATCH_BINARY(opcode, Ty) \
  if (opname == opcode && bvexpr->ty == BvExpr::Ty) { \
    return matchExpr(list->elements[1], bvexpr->l) && \
           matchExpr(list->elements[2], bvexpr->r); \
  }

#define MATCH_UNARY(opcode, Ty) \
  if (opname == opcode && bvexpr->ty == BvExpr::Ty) { \
    return matchExpr(list->elements[1], bvexpr->l); \
  }

#define EVAL_TERNARY(opcode, expr) \
  if (opname == "!" opcode) { \
    int a = evalExpr(list->elements[1]); \
    int b = evalExpr(list->elements[2]); \
    int c = evalExpr(list->elements[3]); \
    return expr; \
  }

#define EVAL_BINARY(opcode, expr) \
  if (opname == "!" opcode) { \
    int a = evalExpr(list->elements[1]); \
    int b = evalExpr(list->elements[2]); \
    return a expr b; \
  }

#define EVAL_UNARY(opcode, expr) \
  if (opname == "!" opcode) { \
    int a = evalExpr(list->elements[1]); \
    return expr a; \
  }

#define BUILD_TERNARY(opcode, Ty) \
  if (opname == opcode) { \
    BvExpr *a = buildExpr(list->elements[1]); \
    BvExpr *b = buildExpr(list->elements[2]); \
    BvExpr *c = buildExpr(list->elements[3]); \
    return ctx->create(BvExpr::Ty, a, b, c); \
  }

#define BUILD_BINARY(opcode, Ty) \
  if (opname == opcode) { \
    BvExpr *a = buildExpr(list->elements[1]); \
    BvExpr *b = buildExpr(list->elements[2]); \
    return ctx->create(BvExpr::Ty, a, b); \
  }

#define BUILD_UNARY(opcode, Ty) \
  if (opname == opcode) { \
    BvExpr * a = buildExpr(list->elements[1]); \
    return ctx->create(BvExpr::Ty, a); \
  }

BvRule::BvRule(const char *text): text(text) {
  pattern = parse();
}

BvRule::~BvRule() {
  release(pattern);
}

void BvRule::release(Expr *expr) {
  if (auto list = dyn_cast<List>(expr)) {
    for (auto elem : list->elements)
      release(elem);
  }
  delete expr;
}

void BvRule::dump(std::ostream &os) {
  dump(pattern, os);
  os << "\n===== binding starts =====\n";
  for (auto [k, v] : binding) {
    os << k << " = ";
    v->dump(os);
  }
  os << "\n===== binding ends =====\n";
}

void BvRule::dump(Expr *expr, std::ostream &os) {
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

std::string_view BvRule::nextToken() {
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

Expr *BvRule::parse() {
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

bool BvRule::matchExpr(Expr *expr, BvExpr* bvexpr) {
  if (auto* atom = dyn_cast<Atom>(expr)) {
    std::string_view var = atom->value;

    // A normal binding.
    if (var[0] != '\'' && !(std::isdigit(var[0]) || var[0] == '-')) {
      if (binding.count(var))
        return binding[var] == bvexpr;

      binding[var] = bvexpr;
      return true;
    }

    // This denotes a int-constant.
    if (bvexpr->ty != BvExpr::Const) {
      return false;
    }

    // This is a int literal.
    if (std::isdigit(var[0]) || var[0] == '-') {
      std::string str(var);
      if (std::stoi(str) != bvexpr->vi)
        return false;
    }

    if (binding.count(var))
      return binding[var]->vi == bvexpr->vi;

    binding[var] = bvexpr;
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

  MATCH_TERNARY("ite", Ite);
  MATCH_TERNARY("mulmod", MulMod);

  MATCH_BINARY("eq", Eq);
  MATCH_BINARY("ne", Ne);
  MATCH_BINARY("le", Le);
  MATCH_BINARY("lt", Lt);
  MATCH_BINARY("add", Add);
  MATCH_BINARY("sub", Sub);
  MATCH_BINARY("mul", Mul);
  MATCH_BINARY("div", Div);
  MATCH_BINARY("mod", Mod);
  MATCH_BINARY("and", And);
  MATCH_BINARY("or", Or);
  MATCH_BINARY("xor", Xor);
  MATCH_BINARY("lsh", Lsh);
  MATCH_BINARY("rsh", Rsh);

  MATCH_UNARY("not", Not);
  MATCH_UNARY("minus", Minus);

  return false;
}

int BvRule::evalExpr(Expr *expr) {
  if (auto atom = dyn_cast<Atom>(expr)) {
    if (std::isdigit(atom->value[0]) || atom->value[0] == '-') {
      std::string str(atom->value);
      return std::stoi(str);
    }

    if (atom->value[0] == '\'') {
      auto lint = binding[atom->value];
      return lint->vi;
    }
  }

  auto list = dyn_cast<List>(expr);

  assert(list && !list->elements.empty());

  auto head = dyn_cast<Atom>(list->elements[0]);
  std::string_view opname = head->value;

  EVAL_TERNARY("mulmod", ((int64_t) a * (int64_t) b) % c);

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
  EVAL_BINARY("lsh", <<);
  EVAL_BINARY("rsh", >>);

  EVAL_UNARY("not", !);

  if (opname == "!only-if") {
    int a = evalExpr(list->elements[1]);
    if (!a)
      failed = true;
    return 0;
  }

  std::cerr << "unknown opname: " << opname << "\n";
  assert(false);
}


BvExpr *BvRule::buildExpr(Expr *expr) {
  if (auto atom = dyn_cast<Atom>(expr)) {
    // This is an integer literal. Evaluate it.
    if (std::isdigit(atom->value[0]) || atom->value[0] == '-' || atom->value[0] == '\'') {
      int result = evalExpr(expr);
      return ctx->create(BvExpr::Const, result);
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

    return ctx->create(BvExpr::Const, result);
  }

  BUILD_TERNARY("ite", Ite);
  BUILD_TERNARY("mulmod", MulMod);

  BUILD_BINARY("add", Add);
  BUILD_BINARY("sub", Sub);
  BUILD_BINARY("mul", Mul);
  BUILD_BINARY("div", Div);
  BUILD_BINARY("mod", Mod);
  BUILD_BINARY("and", And);
  BUILD_BINARY("or", Or);
  BUILD_BINARY("eq", Eq);
  BUILD_BINARY("ne", Ne);
  BUILD_BINARY("le", Le);
  BUILD_BINARY("lt", Lt);
  BUILD_BINARY("lsh", Lsh);
  BUILD_BINARY("rsh", Rsh);

  BUILD_UNARY("minus", Minus);
  BUILD_UNARY("not", Not);

  std::cerr << "unknown opname: " << opname << "\n";
  assert(false);
}

BvExpr *BvRule::extract(const std::string &name) {
  if (!binding.count(name)) {
    std::cerr << "querying unknown name: " << name << "\n";
    dump();
    assert(false);
  }
  return binding[name];
}

BvExpr *BvRule::rewriteRoot(BvExpr *bvexpr) {
  loc = 0;
  failed = false;
  binding.clear();
  
  auto list = dyn_cast<List>(pattern);
  auto matcher = list->elements[1];
  auto rewriter = list->elements[2];

  if (!matchExpr(matcher, bvexpr))
    return bvexpr;

  BvExpr *opnew = buildExpr(rewriter);
  if (!opnew || failed)
    return bvexpr;

  return opnew;
}

BvExpr *BvRule::rewrite(BvExpr *expr) {
  if (!expr)
    return nullptr;

  BvExpr* newcond = rewrite(expr->cond);
  BvExpr* newl = rewrite(expr->l);
  BvExpr* newr = rewrite(expr->r);

  BvExpr* updated = ctx->create(expr->ty, expr->vi, expr->name, newcond, newl, newr);
  return rewriteRoot(updated);
}
