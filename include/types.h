#pragma once

#include <cstddef>
#include <llvm-21/llvm/IR/LLVMContext.h>
#include <llvm-21/llvm/IR/Type.h>

enum TypeKind { SINGED, UNSINGED, FLOAT, CHAR, STRUCT, VOID };

struct SystemType {

  TypeKind kind;
  size_t size = 0;
  bool is_ptr = false;
  size_t ptrdepth = 0;

  bool is_arr = false;
  size_t size_arr = 0;

  std::string name = "";

  SystemType(TypeKind k, size_t siz, bool ptr, size_t depth, bool arr,
             size_t size, std::string nam)
      : kind(k), size(siz), is_ptr(ptr), ptrdepth(depth), is_arr(arr),
        size_arr(size), name(nam) {}
};
