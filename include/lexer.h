#pragma once 

#include <string>
#include <vector>

enum TokenType {
  NUMBERS,
};

struct Token {
  TokenType Type;
  std::string Value;
};

class Lexer {
public:
  std::string value;
  size_t i = 0;

  Lexer(std::string val) : value(val) {}

  char Peek(size_t peek_next = 0);
  void Consume();

  std::vector<Token> lexer();
};
