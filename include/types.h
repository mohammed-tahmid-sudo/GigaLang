#pragma once

#include <cstddef>
#include <llvm-21/llvm/IR/LLVMContext.h>
#include <llvm-21/llvm/IR/Type.h>

enum TypeKind { SINGED };

struct SystemType {

  TypeKind kind;
  size_t size = 0;

  SystemType(TypeKind k, size_t siz) : kind(k), size(siz) {}
};
