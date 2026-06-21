#pragma once
#include <cstddef>
#include <lexer.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Type.h>
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
  TypeKind kind = VOID;
  bool is_ptr = false;
  size_t ptrdepth = 0;

  bool is_arr = false;
  size_t size_arr = 0;

  std::string struct_name;

};

llvm::Type *ComputeType(SystemType &type, CodegenContext &cc);
void TurnTokenToType(SystemType& type, const Token& token);
