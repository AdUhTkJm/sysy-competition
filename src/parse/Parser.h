#ifndef PARSER_H
#define PARSER_H

#include <iostream>
#include <vector>
#include <cassert>

#include "ASTNode.h"
#include "Lexer.h"
#include "TypeContext.h"

namespace sys {

class Parser {
  std::vector<Token> tokens;
  size_t loc;
  TypeContext &ctx;

  Token last() {
    if (loc - 1 >= tokens.size())
      return Token::End;
    return tokens[loc - 1];
  }

  Token peek() {
    if (loc >= tokens.size())
      return Token::End;
    return tokens[loc];
  }

  Token consume() {
    if (loc >= tokens.size())
      return Token::End;
    return tokens[loc++];
  }
  
  bool peek(Token::Type t) {
    return peek().type == t;
  }

  template<class... Rest>
  bool peek(Token::Type t, Rest... ts) {
    return peek(t) || peek(ts...);
  }

  template<class... T>
  bool test(T... ts) {
    if (peek(ts...)) {
      loc++;
      return true;
    }
    return false;
  }

  Token expect(Token::Type t) {
    if (!test(t)) {
      std::cerr << "expected " << t << ", but got " << peek().type << "\n";
      assert(false);
    }
    return last();
  }

  // Parses only void, int and float.
  Type *parseSimpleType();

  ASTNode *primary();
  ASTNode *mul();
  ASTNode *add();
  ASTNode *rel();
  ASTNode *eq();
  ASTNode *land();
  ASTNode *lor();
  ASTNode *expr();
  ASTNode *stmt();
  BlockNode *block();
  TransparentBlockNode *varDecl();
  FnDeclNode *fnDecl();
  BlockNode *compUnit();

public:
  Parser(const std::string &input, TypeContext &ctx);
  ASTNode *parse();
};

}

#endif
