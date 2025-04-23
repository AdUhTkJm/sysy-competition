#include "Parser.h"
#include "ASTNode.h"
#include "Lexer.h"
#include "Type.h"
#include "TypeContext.h"
#include <vector>

using namespace sys;

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
    return varDecl();

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
  expect(Token::LBrace);
  std::vector<ASTNode *> nodes;
  
  while (!test(Token::RBrace))
    nodes.push_back(stmt());

  return new BlockNode(nodes);
}

TransparentBlockNode *Parser::varDecl() {
  bool mut = test(Token::Const);
  auto base = parseSimpleType();
  std::vector<VarDeclNode*> decls;

  do {
    Type *ty = base;
    std::string name = expect(Token::Ident).vs;
    std::vector<ASTNode *> dimExprs;

    while (test(Token::LBrak)) {
      dimExprs.push_back(expr());
      expect(Token::RBrak);
    }

    if (dimExprs.size() != 0)
      // TODO: do folding immediately
      ty = new ArrayType(ty, dimExprs);

    ASTNode *init = nullptr;
    if (test(Token::Assign))
      init = expr();

    auto decl = new VarDeclNode(name, init, mut);
    decl->type = ty;
    decls.push_back(decl);

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
    std::vector<ASTNode *> dimExprs;

    bool isPointer = false;
    if (test(Token::LBrak)) {
      isPointer = true;
      expect(Token::RBrak);
    }

    while (test(Token::LBrak)) {
      dimExprs.push_back(expr());
      expect(Token::RBrak);
    }

    if (dimExprs.size() != 0)
      ty = new ArrayType(ty, dimExprs);
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
      nodes.push_back(varDecl());
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

    nodes.push_back(varDecl());
  }

  return new BlockNode(nodes);
}

Parser::Parser(const std::string &input, TypeContext &ctx): loc(0), ctx(ctx) {
  Lexer lex(input);

  while (lex.hasMore())
    tokens.push_back(lex.nextToken());
}

ASTNode *Parser::parse() {
  auto unit = compUnit();

  // Release memory of tokens.
  for (auto tok : tokens) {
    if (tok.type == Token::Ident)
      delete[] tok.vs;
  }

  return unit;
}
