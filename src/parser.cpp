#include "lexer.h"
#include <ast.h>
#include <iostream>
#include <llvm-21/llvm/IR/Intrinsics.h>
#include <memory>
#include <parser.h>
#include <string>

Token Parser::Peek(size_t forward) {
  if (counter > input.size() && counter + forward > input.size()) {
    std::cerr
        << "LOOKING WAYYY TOO AHEAD\n                   ---FROM THE PARSER\n";
  }
  return input[counter + forward];
}

void Parser::Consume() {
  if (counter + 1 > input.size()) {
    std::cerr
        << "COMSUMING TOO MUCH \n                        ---FROM THE PARSER\n";
  }

  counter++;
  return;
}

std::vector<std::unique_ptr<ast>> Parser::parse() {
  std::vector<std::unique_ptr<ast>> output;

  if (Peek().Type == NUMBERS) {
    std::unique_ptr<IntegerNode> val =
        std::make_unique<IntegerNode>(std::stoi(Peek().Value), true);

    output.push_back(std::move(val));
  }

  return output;
}
