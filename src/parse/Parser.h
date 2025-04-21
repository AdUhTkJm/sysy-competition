#ifndef PARSER_H
#define PARSER_H

#include <vector>
#include <cassert>

#include "ASTNode.h"
#include "Lexer.h"

namespace sys {

class Parser {
  std::vector<Token> tokens;
  size_t loc;

  Token last() {
    return tokens[loc - 1];
  }

  Token peek() {
    return tokens[loc];
  }

  Token consume() {
    return tokens[loc++];
  }
  
  bool peek(Token::Type t) {
    return tokens[loc].type == t;
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

  void expect(Token::Type t) {
    if (!test(t))
      assert(false);
  }

  BlockNode *compUnit();

public:
  Parser(const std::string &input);
  ASTNode *parse();
};

}

#endif
