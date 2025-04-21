#include "Lexer.h"
#include <cassert>
#include <cmath>
#include <map>

using namespace sys;

std::map<std::string, Token::Type> keywords = {
  { "if", Token::If },
  { "else", Token::Else },
  { "while", Token::While },
  { "for", Token::For },
  { "return", Token::Return },
  { "int", Token::Int },
  { "float", Token::Float },
  { "const", Token::Const },
};

Token Lexer::nextToken() {
  assert(loc < input.size());

  // Skip whitespace
  while (loc < input.size() && std::isspace(input[loc]))
    loc++;

  // Hit end of input because of skipping whitespace
  if (loc >= input.size())
    return Token::End;

  char c = input[loc];

  // Identifiers and keywords
  if (std::isalpha(c) || c == '_') {
    std::string name;
    while (loc < input.size() && (std::isalnum(input[loc]) || input[loc] == '_'))
      name += input[loc++];

    if (keywords.count(name))
      return keywords[name];

    return Token(name);
  }

  // Integer literals
  if (std::isdigit(c)) {
    int value = 0;
    if (c == '0') {
      if (input[loc + 1] == 'x' || input[loc + 1] == 'X') {
        // Hexadecimal, skip '0x'
        loc += 2;
        while (std::isdigit(input[loc]) || ('a' <= tolower(input[loc]) && tolower(input[loc]) <= 'f')) {
          value = value * 16 + (std::isdigit(input[loc]) ? input[loc] - '0' : tolower(input[loc]) - 'a');
          loc++;
        }
        return value;
      }

      // Octal
      while (std::isdigit(input[loc])) {
        value = value * 8 + (input[loc] - '0');
        loc++;
      }
      return value;
    }

    // Decimal, but might be floating point
    while (std::isdigit(input[loc])) {
      value = value * 10 + (input[loc] - '0');
      loc++;
    }
    
    if (input[loc] == '.') {
      // Floating point, skip '.'
      loc++;
      float f = value;
      float base = 1, decimal = 0;
      while (std::isdigit(input[loc])) {
        decimal += (input[loc] - '0') / (base *= 10);
        loc++;
      }

      // Check for 1.23e(+|-)?8
      if (input[loc] == 'e') {
        // Skip 'e'
        loc++;
        int sign = 1;
        if (input[loc] == '+')
          loc++;
        else if (input[loc] == '-')
          loc++, sign = -1;

        // Now an integer, just read it
        int exponent = 0;
        while (std::isdigit(input[loc])) {
          exponent = exponent * 10 + (input[loc] - '0');
          loc++;
        }
        return powf(10, exponent) * (f + decimal);
      }

      return f + decimal;
    }

    return value;
  }

  // Check for multi-character operators like >=, <=, ==, !=, +=, etc.
  if (loc + 1 < input.size()) {
    switch (c) {
    case '=': 
      if (input[loc + 1] == '=') { loc += 2; return Token::Eq; }
      break;
    case '>':
      if (input[loc + 1] == '=') { loc += 2; return Token::Ge; }
      break;
    case '<': 
      if (input[loc + 1] == '=') { loc += 2; return Token::Le; }
      break;
    case '!': 
      if (input[loc + 1] == '=') { loc += 2; return Token::Ne; }
      break;
    case '+': 
      if (input[loc + 1] == '=') { loc += 2; return Token::PlusEq; }
      break;
    case '-': 
      if (input[loc + 1] == '=') { loc += 2; return Token::MinusEq; }
      break;
    case '*': 
      if (input[loc + 1] == '=') { loc += 2; return Token::MulEq; }
      break;
    case '/': 
      if (input[loc + 1] == '=') { loc += 2; return Token::DivEq; }
      if (input[loc + 1] == '/') { 
        // Loop till we find a line break, then retries to find the next Token
        // (we can't continue working in the same function frame)
        for (; loc < input.size(); loc++) {
          if (input[loc] == '\n')
            return nextToken();
        }
      }
      if (input[loc + 1] == '*') {
        // Skip '/*', and loop till we find '*/'.
        loc += 2;
        for (; loc < input.size(); loc++) {
          if (input[loc] == '*' && input[loc + 1] == '/')
            return nextToken();
        }
      }
      break;
    case '%': 
      if (input[loc + 1] == '=') { loc += 2; return Token::ModEq; }
      break;
    case '&': 
      if (input[loc + 1] == '&') { loc += 2; return Token::And; }
      break;
    case '|': 
      if (input[loc + 1] == '|') { loc += 2; return Token::Or; }
      break;
    default:
      break;
    }
  }

  // Single-character operators and symbols
  switch (c) {
  case '+': loc++; return Token::Plus;
  case '-': loc++; return Token::Minus;
  case '*': loc++; return Token::Mul;
  case '/': loc++; return Token::Div;
  case '%': loc++; return Token::Mod;
  case ';': loc++; return Token::Semicolon;
  case '=': loc++; return Token::Assign;
  case '!': loc++; return Token::Not;
  case '(': loc++; return Token::LPar;
  case ')': loc++; return Token::RPar;
  case '[': loc++; return Token::LBrak;
  case ']': loc++; return Token::RBrak;
  case '<': loc++; return Token::Lt;
  case '>': loc++; return Token::Gt;
  case ',': loc++; return Token::Comma;
  case '{': loc++; return Token::LBrace;
  case '}': loc++; return Token::RBrace;
  default:
    assert(false);
  }
}

bool Lexer::hasMore() const {
  return loc < input.size();
}
