#include "types.h"
#include <iostream>
#include <llvm-21/llvm/IR/LLVMContext.h>
#include <memory>
#include <utils.h>

std::unique_ptr<SystemType> CreateSystemType(TypeKind k, size_t siz, bool ptr,
                                             size_t depth, bool arr,
                                             size_t size, std::string nam) {
  return std::make_unique<SystemType>(k, siz, ptr, depth, arr, size, nam);
}
