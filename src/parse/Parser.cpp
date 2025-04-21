#include "Parser.h"
#include "ASTNode.h"
#include "Lexer.h"
#include "Type.h"
#include <vector>

using namespace sys;

Type *Parser::parseSimpleType() {
switch (consume().type) {
  case Token::Void:
    return new VoidType();
  case Token::Int:
    return new IntType();
  case Token::Float:
    return new FloatType();
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
  auto n = primary();
  while (peek(Token::Plus, Token::Minus)) {
    switch (consume().type) {
    case Token::Plus:
      n = new BinaryNode(BinaryNode::Mul, n, mul());
      break;
    case Token::Minus:
      n = new BinaryNode(BinaryNode::Div, n, mul());
      break;
    default:
      assert(false);
    }
  }
  return n;
}

ASTNode *Parser::expr() {
  return add();
}

ASTNode *Parser::stmt() {
  if (test(Token::Return)) {
    auto ret = new ReturnNode(expr());
    expect(Token::Semicolon);
    return ret;
  }

  if (peek(Token::Const, Token::Int, Token::Float))
    return varDecl();

  auto n = expr();
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
      ty = new PointerType(ty);

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

Parser::Parser(const std::string &input): loc(0) {
  Lexer lex(input);

  while (lex.hasMore())
    tokens.push_back(lex.nextToken());
}

ASTNode *Parser::parse() {
  return compUnit();
}
