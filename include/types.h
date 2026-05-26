#pragma once
#include <cstddef>
#include <lexer.h>
#include <llvm-18/llvm/IR/Intrinsics.h>
#include <llvm-18/llvm/IR/Type.h>
#include <string>
#include <utils.h>

enum TypeKind {
  INTEGER,
  FLOAT,
  BOOLEAN,
  VOID,
  CHAR,
  STRUCTTY,
};

struct SystemType {
  TypeKind kind;
  bool is_ptr = false;
  size_t ptrdepth = 0;

  bool is_arr = false;
  size_t size_arr = 0;

  std::string struct_name;

  llvm::Type *theLLvmtType = nullptr;
};

llvm::Type *ComputeType(SystemType &type, CodegenContext &cc);

void TurnTokenToType(SystemType type, Token token) {
  switch (token.type) {
  case IDENTIFIER:
    type.kind = TypeKind::STRUCTTY;
    break;
  case TYPES: {
  }
  }
  return;
}
