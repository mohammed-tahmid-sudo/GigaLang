#pragma once

#include <ast.h>
#include <lexer.h>
#include <vector>

class Parser {
public:
  std::vector<Token> input;
  size_t counter = 0;

  Parser(std::vector<Token> inp) : input(inp) {}

  Token Peek(size_t forward = 0);
  void Consume();

  std::vector<std::unique_ptr<ast>> parse();
};
