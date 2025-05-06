#include "Lexer.h"
#include <cassert>
#include <cctype>
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
  { "void", Token::Void },
  { "const", Token::Const },
  { "break", Token::Break },
  { "continue", Token::Continue },
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

  // Integer/FP literals
  if (std::isdigit(c) || c == '.') {
    int start = loc;
    bool isFloat = false;

    if (c == '0') {
      if (input[loc + 1] == 'x' || input[loc + 1] == 'X') {
        // Hexadecimal, skip '0x'
        loc += 2;
        while (std::isxdigit(input[loc]) || input[loc] == '.') {
          if (input[loc] == '.') {
            // Already seen a '.' before. Shouldn't continue.
            if (isFloat)
              break;
            
            isFloat = true;
          }
          loc++;
        }

        // Try to read a 'p' for exponent.
        if (input[loc] == 'p' || input[loc] == 'P') {
          isFloat = true;
          loc++;

          if (input[loc] == '+' || input[loc] == '-')
            loc++;
          
          while (std::isdigit(input[loc])) 
            loc++;
        }

        std::string raw = input.substr(start, loc - start);
        return isFloat ? Token(strtof(raw.c_str(), nullptr)) : std::stoi(raw, nullptr, /*base = autodetect*/0);
      }

      // Octal. But let `std::stoi` to check for it.
      // Fall through here.
    }

    // Now this is a normal decimal integer or FP.
    while (std::isdigit(input[loc]) || input[loc] == '.') {
      if (input[loc] == '.') {
        // Already seen a '.' before. Shouldn't continue.
        if (isFloat)
          break;
        
        isFloat = true;
      }
      loc++;
    }

    // Try to read an 'e' for exponent.
    if (input[loc] == 'e' || input[loc] == 'E') {
      isFloat = true;
      loc++;

      if (input[loc] == '+' || input[loc] == '-')
        loc++;
      
      while (std::isdigit(input[loc])) 
        loc++;
    }

    std::string raw = input.substr(start, loc - start);
    return isFloat ? Token(strtof(raw.c_str(), nullptr)) : std::stoi(raw, nullptr, /*base = autodetect*/0);
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
          if (input[loc] == '*' && input[loc + 1] == '/') {
            // Skip '*/'.
            loc += 2;
            return nextToken();
          }
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
