#include "Parser.h"
#include "ASTNode.h"
#include "Lexer.h"
#include "Type.h"
#include "TypeContext.h"
#include <iostream>
#include <vector>

using namespace sys;

int ConstValue::size() {
  int total = 1;
  for (auto x : dims)
    total *= x;
  return total;
}

int ConstValue::stride() {
  int stride = 1;
  for (size_t i = 1; i < dims.size(); i++)
    stride *= dims[i];
  return stride;
}

ConstValue ConstValue::operator[](int i) {
  assert(dims.size() >= 1);

  std::vector<int> newDims;
  newDims.reserve(dims.size() - 1);

  for (size_t i = 1; i < dims.size(); i++) 
    newDims.push_back(i);
  
  return ConstValue(vi + stride(), newDims);
};

int *ConstValue::getRaw() {
  auto total = size();
  auto result = new int[total];
  memcpy(result, vi, total * sizeof(int));
  return result;
}

float *ConstValue::getRawFloat() {
  auto total = size();
  auto result = new float[total];
  memcpy(result, vi, total * sizeof(float));
  return result;
}

void ConstValue::release() {
  delete[] vi;
}

Token Parser::last() {
  if (loc - 1 >= tokens.size())
    return Token::End;
  return tokens[loc - 1];
}

Token Parser::peek() {
  if (loc >= tokens.size())
    return Token::End;
  return tokens[loc];
}

Token Parser::consume() {
  if (loc >= tokens.size())
    return Token::End;
  return tokens[loc++];
}

bool Parser::peek(Token::Type t) {
  return peek().type == t;
}

Token Parser::expect(Token::Type t) {
  if (!test(t)) {
    std::cerr << "expected " << t << ", but got " << peek().type << "\n";
    printSurrounding();
    assert(false);
  }
  return last();
}

void Parser::printSurrounding() {
  std::cerr << "surrounding:\n";
  for (size_t i = std::max(0ul, loc - 5); i < std::min(tokens.size(), loc + 6); i++)
    std::cerr << tokens[i].type << (i == loc ? "(here)" : "") << "\n";
}

Type *Parser::parseSimpleType() {
switch (consume().type) {
  case Token::Void:
    return ctx.create<VoidType>();
  case Token::Int:
    return ctx.create<IntType>();
  case Token::Float:
    return ctx.create<FloatType>();
  default:
    std::cerr << "unknown type: " << peek().type << "\n";
    assert(false);
  }
}

ConstValue Parser::getArrayInit(const std::vector<int> &dims) {
  auto carry = [&](std::vector<int> &x) {
    for (int i = (int) x.size() - 1; i >= 1; i--) {
      if (x[i] >= dims[i]) {
        auto quot = x[i] / dims[i];
        x[i] %= dims[i];
        x[i - 1] += quot;
      }
    }
  };

  auto offset = [&](const std::vector<int> &x) {
    int total = 0, stride = 1;
    for (int i = (int) x.size() - 1; i >= 0; i--) {
      total += x[i] * stride;
      stride *= dims[i];
    }
    return total;
  };

  // Initialize with `dims.size()` zeroes.
  std::vector<int> place(dims.size(), 0);
  int size = 1;
  for (auto x : dims)
    size *= x;
  int *vi = new int[size];
  memset(vi, 0, size * sizeof(int));

  // add 1 to `place[addAt]` when we meet the next `}`.
  int addAt = dims.size();
  do {
    if (test(Token::LBrace)) {
      addAt--;
      continue;
    }

    if (test(Token::RBrace)) {
      // Bump `place[addAt]`, and set everything after it to 0.
      place[addAt]++;
      for (int i = addAt + 1; i < dims.size(); i++)
        place[i] = 0;
      carry(place);

      addAt++;
      
      // If this `}` isn't at the end, then a `,` must follow.
      if (addAt != dims.size())
        expect(Token::Comma);
      continue;
    }

    // Automatically carry.
    vi[offset(place)] = earlyFold(expr()).getInt();
    place[place.size() - 1]++;
    carry(place);
    if (!test(Token::Comma) && !peek(Token::RBrace))
      expect(Token::RBrace);
  } while (addAt != dims.size());
  return ConstValue(vi, dims);
}

ASTNode *Parser::primary() {
  if (peek(Token::LInt))
    return new IntNode(consume().vi);
  
  if (test(Token::LPar)) {
    auto n = expr();
    expect(Token::RPar);
    return n;
  }

  if (peek(Token::Ident))
    return new VarRefNode(consume().vs);

  std::cerr << "unexpected token " << peek().type << "\n";
  printSurrounding();
  assert(false);
}

ASTNode *Parser::mul() {
  auto n = primary();
  while (peek(Token::Mul, Token::Div, Token::Mod)) {
    switch (consume().type) {
    case Token::Mul:
      n = new BinaryNode(BinaryNode::Mul, n, primary());
      break;
    case Token::Div:
      n = new BinaryNode(BinaryNode::Div, n, primary());
      break;
    case Token::Mod:
      n = new BinaryNode(BinaryNode::Mod, n, primary());
      break;
    default:
      assert(false);
    }
  }
  return n;
}

ASTNode *Parser::add() {
  auto n = mul();
  while (peek(Token::Plus, Token::Minus)) {
    switch (consume().type) {
    case Token::Plus:
      n = new BinaryNode(BinaryNode::Add, n, mul());
      break;
    case Token::Minus:
      n = new BinaryNode(BinaryNode::Sub, n, mul());
      break;
    default:
      assert(false);
    }
  }
  return n;
}

ASTNode *Parser::rel() {
  auto n = add();
  while (peek(Token::Lt, Token::Gt, Token::Ge, Token::Le)) {
    switch (consume().type) {
    case Token::Lt:
      n = new BinaryNode(BinaryNode::Lt, n, add());
      break;
    case Token::Le:
      n = new BinaryNode(BinaryNode::Le, n, add());
      break;
    case Token::Gt:
      n = new BinaryNode(BinaryNode::Lt, add(), n);
      break;
    case Token::Ge:
      n = new BinaryNode(BinaryNode::Le, add(), n);
      break;
    default:
      assert(false);
    }
  }
  return n;
}

ASTNode *Parser::eq() {
  auto n = rel();
  while (peek(Token::Eq, Token::Ne)) {
    switch (consume().type) {
    case Token::Eq:
      n = new BinaryNode(BinaryNode::Eq, n, rel());
      break;
    case Token::Ne:
      n = new BinaryNode(BinaryNode::Ne, n, rel());
      break;
    default:
      assert(false);
    }
  }
  return n;
}

ASTNode *Parser::expr() {
  return eq();
}

ASTNode *Parser::stmt() {
  if (peek(Token::LBrace))
    return block();

  if (test(Token::Return)) {
    auto ret = new ReturnNode(expr());
    expect(Token::Semicolon);
    return ret;
  }

  if (test(Token::If)) {
    expect(Token::LPar);
    auto cond = expr();
    expect(Token::RPar);
    auto ifso = stmt();
    ASTNode *ifnot = nullptr;
    if (test(Token::Else))
      ifnot = stmt();
    return new IfNode(cond, ifso, ifnot);
  }

  if (test(Token::While)) {
    expect(Token::LPar);
    auto cond = expr();
    expect(Token::RPar);
    auto body = stmt();
    return new WhileNode(cond, body);
  }

  if (peek(Token::Const, Token::Int, Token::Float))
    return varDecl(false);

  auto n = expr();
  if (test(Token::Assign)) {
    if (!isa<VarRefNode>(n)) {
      std::cerr << "expected lval\n";
      assert(false);
    }
    auto value = expr();
    expect(Token::Semicolon);
    return new AssignNode(n, value);
  }

  expect(Token::Semicolon);
  return n;
}

BlockNode *Parser::block() {
  SemanticScope scope(*this);

  expect(Token::LBrace);
  std::vector<ASTNode *> nodes;
  
  while (!test(Token::RBrace))
    nodes.push_back(stmt());

  return new BlockNode(nodes);
}

TransparentBlockNode *Parser::varDecl(bool global) {
  bool mut = !test(Token::Const);
  auto base = parseSimpleType();
  std::vector<VarDeclNode*> decls;

  do {
    Type *ty = base;
    std::string name = expect(Token::Ident).vs;
    std::vector<int> dims;

    while (test(Token::LBrak)) {
      dims.push_back(earlyFold(expr()).getInt());
      expect(Token::RBrak);
    }

    if (dims.size() != 0)
      // TODO: do folding immediately
      ty = new ArrayType(ty, dims);

    ASTNode *init = nullptr;
    if (test(Token::Assign))
      init = isa<ArrayType>(ty) ? new ConstArrayNode(getArrayInit(dims).getRaw()) : expr();

    auto decl = new VarDeclNode(name, init, mut, global);
    decl->type = ty;
    decls.push_back(decl);

    // Record in symbol table.
    if (!mut && isa<IntType>(base))
      symbols[name] = earlyFold(init);

    if (!test(Token::Comma) && !peek(Token::Semicolon))
      expect(Token::Comma);
  } while (!test(Token::Semicolon));

  return new TransparentBlockNode(decls);
}

FnDeclNode *Parser::fnDecl() {
  Type *ret = parseSimpleType();

  auto name = expect(Token::Ident).vs;

  std::vector<std::string> args;
  std::vector<Type*> params;

  expect(Token::LPar);
  while (!test(Token::RPar)) {
    auto ty = parseSimpleType();
    args.push_back(expect(Token::Ident).vs);
    std::vector<int> dims;

    bool isPointer = false;
    if (test(Token::LBrak)) {
      isPointer = true;
      expect(Token::RBrak);
    }

    while (test(Token::LBrak)) {
      dims.push_back(earlyFold(expr()).getInt());
      expect(Token::RBrak);
    }

    if (dims.size() != 0)
      ty = new ArrayType(ty, dims);
    if (isPointer)
      ty = ctx.create<PointerType>(ty);

    params.push_back(ty);

    if (!test(Token::Comma) && !peek(Token::RPar))
      expect(Token::Comma);
  }

  auto decl = new FnDeclNode(name, args, block());
  decl->type = new FunctionType(ret, params);
  return decl;
}

BlockNode *Parser::compUnit() {
  std::vector<ASTNode*> nodes;
  while (!test(Token::End)) {
    if (peek(Token::Const)) {
      nodes.push_back(varDecl(true));
      continue;
    }

    // For functions, it would be:
    //   Type ident `(`
    // while for variables it's `=`.
    // Moreover, the Type is only a single token,
    // so we lookahead for 2 tokens.
    if (tokens[loc + 2].type == Token::LPar) {
      nodes.push_back(fnDecl());
      continue;
    }

    nodes.push_back(varDecl(true));
  }

  return new BlockNode(nodes);
}

// Yes, heavy memory leak... But who cares?
// We can't just call release(), otherwise we'd release everything in the symbol table.
ConstValue Parser::earlyFold(ASTNode *node) {
  if (auto ref = dyn_cast<VarRefNode>(node)) {
    if (!symbols.count(ref->name)) {
      std::cerr << "cannot find const: " << ref->name << "\n";
      assert(false);
    }
    return symbols[ref->name];
  }

  if (auto binary = dyn_cast<BinaryNode>(node)) {
    auto l = earlyFold(binary->l).getInt();
    auto r = earlyFold(binary->r).getInt();
    switch (binary->kind) {
    case BinaryNode::Add:
      return ConstValue(new int(l + r), {});
    case BinaryNode::Sub:
      return ConstValue(new int(l - r), {});
    case BinaryNode::Mul:
      return ConstValue(new int(l * r), {});
    case BinaryNode::Div:
      return ConstValue(new int(l / r), {});
    case BinaryNode::Mod:
      return ConstValue(new int(l % r), {});
    case BinaryNode::And:
      return ConstValue(new int(l && r), {});
    case BinaryNode::Or:
      return ConstValue(new int(l || r), {});
    case BinaryNode::Eq:
      return ConstValue(new int(l == r), {});
    case BinaryNode::Ne:
      return ConstValue(new int(l != r), {});
    case BinaryNode::Lt:
      return ConstValue(new int(l < r), {});
    case BinaryNode::Le:
      return ConstValue(new int(l > r), {});
    }
  }

  if (auto lint = dyn_cast<IntNode>(node)) {
    return ConstValue(new int(lint->value), {});
  }

  if (auto access = dyn_cast<ArrayAccessNode>(node)) {
    auto array = earlyFold(access->array);
    int index = earlyFold(access->index).getInt();
    return array[index];
  }

  std::cerr << "not constexpr: " << node->getID() << "\n";
  assert(false);
}

Parser::Parser(const std::string &input, TypeContext &ctx): loc(0), ctx(ctx) {
  Lexer lex(input);

  while (lex.hasMore())
    tokens.push_back(lex.nextToken());
}

ASTNode *Parser::parse() {
  auto unit = compUnit();

  // Release memory.
  for (auto tok : tokens) {
    if (tok.type == Token::Ident)
      delete[] tok.vs;
  }

  for (auto [name, constVal] : symbols)
    constVal.release();

  return unit;
}
