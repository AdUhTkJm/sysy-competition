#ifndef PARSER_H
#define PARSER_H

#include <iostream>
#include <map>
#include <vector>
#include <cassert>

#include "ASTNode.h"
#include "Lexer.h"
#include "TypeContext.h"

namespace sys {

// A compile-time integer constant, used when early-folding array dimensions.
// Only integer is allowed, as the language specifies.
class ConstValue {
  union {
    int *vi;
    float *vf;
  };
  std::vector<int> dims;
public:
  bool isFloat;

  ConstValue() {}
  ConstValue(int *vi, const std::vector<int> &dims): vi(vi), dims(dims), isFloat(false) {}
  ConstValue(float *vf, const std::vector<int> &dims): vf(vf), dims(dims), isFloat(true) {}

  ConstValue operator[](int i);
  int getInt();
  float getFloat();
  const auto &getDims() { return dims; }

  int size();
  int stride();

  // Copies a new memory. Doesn't release the original one.
  int *getRaw();
  float *getRawFloat();
  void *getRawRef() { return vi; }

  // Note that we don't use a destructor,
  // because the Parser object will live till end of program.
  void release();
};

class Parser {
  using SymbolTable = std::map<std::string, ConstValue>;
  SymbolTable symbols;

  class SemanticScope {
    Parser &parser;
    SymbolTable symbols;
  public:
    SemanticScope(Parser &parser): parser(parser), symbols(parser.symbols) {};
    ~SemanticScope() { parser.symbols = symbols; }
  };

  std::vector<Token> tokens;
  size_t loc;
  TypeContext &ctx;
  
  std::string currentFunc;

  Token last();
  Token peek();
  Token consume();
  
  bool peek(Token::Type t);
  Token expect(Token::Type t);

  // Prints tokens in range [loc-5, loc+5]. For debugging purposes.
  void printSurrounding();

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

  // Parses only void, int and float.
  Type *parseSimpleType();

  // Const-fold the node.
  ConstValue earlyFold(ASTNode *node);

  ASTNode *primary();
  ASTNode *unary();
  ASTNode *mul();
  ASTNode *add();
  ASTNode *rel();
  ASTNode *eq();
  ASTNode *land();
  ASTNode *lor();
  ASTNode *expr();
  ASTNode *stmt();
  BlockNode *block();
  TransparentBlockNode *varDecl(bool global);
  FnDeclNode *fnDecl();
  BlockNode *compUnit();

  // Global array is guaranteed to be constexpr, so we return a list of int/floats.
  // Local array isn't; so we return a list of ASTNodes.
  void *getArrayInit(const std::vector<int> &dims, bool expectFloat, bool doFold);

public:
  Parser(const std::string &input, TypeContext &ctx);
  ASTNode *parse();
};

}

#endif
