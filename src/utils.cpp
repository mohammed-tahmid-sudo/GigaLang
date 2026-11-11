#include "types.h"
#include <iostream>
#include <llvm-21/llvm/IR/LLVMContext.h>
#include <memory>
#include <utils.h>

std::unique_ptr<SystemType> CreateSystemType(TypeKind kind, size_t size) {
  // SystemType ret;
  //
  // ret.kind = kind;
  // ret.size = size;

  return std::make_unique<SystemType>(kind, size);
}
