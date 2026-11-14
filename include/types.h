#pragma once
#include <cstddef>
#include <cstdint>
#include <lexer.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Type.h>
#include <string>

enum TypeKind { SINGED, UNSINGED, FLOAT, CHAR, STRUCTS, VOID };

struct SystemType {
  TypeKind kind = VOID;
  std::uint16_t size = 0;

  bool is_ptr = false;
  size_t ptrdepth = 0;

  bool is_arr = false;
  size_t size_arr = 0;

  std::string struct_name;
};

void TurnTokenToType(SystemType &type, const Token &token);
