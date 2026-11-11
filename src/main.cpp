#include "parser.h"
#include "utils.h"
#include <iostream>
#include <lexer.h>
#include <llvm-21/llvm/IR/IRBuilder.h>
#include <llvm-21/llvm/IR/LLVMContext.h>
#include <llvm-21/llvm/IR/Module.h>
#include <memory>
#include <string>
#include <vector>

int main() {

  std::string s = "12121";
  Lexer lexer(s);
  std::vector<Token> tokens = lexer.lexer();

  Parser parser(tokens);

  std::vector<std::unique_ptr<ast>> out = parser.parse();

  CodegenContext cc;

  for (size_t i = 0; i < out.size(); i++) {
    out[i]->codegen(cc);
  }

  cc.Module->dump();

  return 0;
}
