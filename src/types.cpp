#include "ast.h"
#include <llvm-18/llvm/IR/DerivedTypes.h>
#include <llvm-18/llvm/IR/Metadata.h>
#include <types.h>

llvm::Type *ComputeType(SystemType &type, CodegenContext &cc) {
  if (type.theLLvmtType) {
    return type.theLLvmtType;
  }

  llvm::Type *theLLvmType = nullptr;

  switch (type.kind) {
  case INTEGER:
    theLLvmType = llvm::Type::getInt32Ty(*cc.TheContext);
    break;
  case FLOAT:
    theLLvmType = llvm::Type::getFloatTy(*cc.TheContext);
    break;
  case CHAR:
    theLLvmType = llvm::Type::getInt8Ty(*cc.TheContext);
    break;
  case BOOLEAN:
    theLLvmType = llvm::Type::getInt1Ty(*cc.TheContext);
    break;
  case VOID:
    theLLvmType = llvm::Type::getVoidTy(*cc.TheContext);
    break;
  case STRUCTTY:
    theLLvmType = cc.lookupStruct(type.struct_name);
    break;
  }

  // 1. Wrap as an array first if needed
  if (type.is_arr) {
    theLLvmType = llvm::ArrayType::get(theLLvmType, type.size_arr);
  }

  // 2. Wrap as a pointer last.
  // In LLVM 18+, any pointer depth collapses into a single generic 'ptr'
  if (type.is_ptr || type.ptrdepth > 0) {
    theLLvmType = llvm::PointerType::get(*cc.TheContext, 0);
  }

  type.theLLvmtType = theLLvmType;

  return theLLvmType;
}
