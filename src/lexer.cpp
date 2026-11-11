#include <cctype>
#include <codecvt>
#include <iostream>
#include <lexer.h>
#include <string>
#include <type_traits>
#include <vector>

char Lexer::Peek(size_t peek_next) {
  if (i > value.size() || peek_next > value.size()) {
    std::cerr << "OUT OF REACH \n        -FROM THE LEXER \n";
    return '\0';
  } else if (peek_next > 0 && value.size() > i + peek_next) {
    return value[i + peek_next];
  } else {
    return value[i];
  }
}

void Lexer::Consume() {
  if (i < value.size()) {
    i++;
  } else {
    std::cerr << "Cannot Consume Beyond The Value size \n            -FROM THE "
                 "LEXER\n";
  }
}

std::vector<Token> Lexer::lexer() {
  std::vector<Token> output;

  while (Peek()) {
    if (std::isdigit(Peek())) {
      std::string holder;
      while (isdigit(Peek())) {
        holder += Peek();
        Consume();
      }
      output.push_back({NUMBERS, holder});
    } else {
      Consume();
    }
  }

  return output;
}
