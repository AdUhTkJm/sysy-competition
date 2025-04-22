#ifndef LEXER_H
#define LEXER_H

#include <vector>
#include <string>
#include <cstring>

namespace sys {

struct Token {
  enum Type {
    // Literals
    LInt, LFloat, Ident,

    // Operators
    Plus, Minus, Mul, Div, Mod,
    PlusEq, MinusEq, MulEq, DivEq, ModEq,
    Le, Ge, Gt, Lt, Eq, Ne,
    And, Or, Semicolon, Assign, Not,
    LPar, RPar, LBrak, RBrak, LBrace, RBrace,
    Comma,

    // Keywords
    If, Else, While, For, Return, Int, Float, Void,
    Const,

    // EOF
    End,
  } type;

  // We don't use std::string here to save space -
  // We have to manually free space anyway. (Destructors won't be called inside union.)
  union {
    int vi;
    float vf;
    char *vs;
  };

  /* implicit */ Token(Type t): type(t) {}
  /* implicit */ Token(int vi): type(LInt), vi(vi) {}
  /* implicit */ Token(float vf): type(LFloat), vf(vf) {}
  /* implicit */ Token(std::string str): type(Ident), vs(new char[str.size() + 1]) {
    strcpy(vs, str.c_str());
  }
};

class Lexer {
  std::string input;

  // Index of `input`
  size_t loc;

public:
  Lexer(const std::string &input): input(input), loc(0) {}

  Token nextToken();
  bool hasMore() const;
};

}

#endif
