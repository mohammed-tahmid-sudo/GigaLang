#include <llvm-21/llvm/IR/Type.h>
#include <utils.h>

llvm::Type *ComputeType(SystemType &type, CodegenContext &cc) {
  llvm::Type *baseType = nullptr;

  switch (type.kind) {
  case SINGED:
  case UNSINGED:
    baseType = llvm::Type::getIntNTy(*cc.TheContext, type.size);
    break;

  case FLOAT:
    switch (type.size) {
    case 16:
      baseType = llvm::Type::getHalfTy(*cc.TheContext);
      break;
    case 32:
      baseType = llvm::Type::getFloatTy(*cc.TheContext);
      break;
    case 64:
      baseType = llvm::Type::getDoubleTy(*cc.TheContext);
      break;
    case 128:
      baseType = llvm::Type::getFP128Ty(*cc.TheContext);
      break;
    default:
      throw std::runtime_error("Unsupported floating-point size");
    }
    break;

  case CHAR:
    baseType = llvm::Type::getInt8Ty(*cc.TheContext);
    break;

  case STRUCTS:
    baseType = cc.lookupStruct(type.struct_name);
    break;

  case VOID:
    baseType = llvm::Type::getVoidTy(*cc.TheContext);
    break;
  }

  if (!baseType)
    throw std::runtime_error("Failed to compute LLVM type");

  if (type.is_arr) {
    if (baseType->isVoidTy())
      throw std::runtime_error("Cannot create an array of void");

    baseType = llvm::ArrayType::get(baseType, type.size_arr);
  }

  if (type.is_ptr) {
    for (size_t i = 0; i < type.ptrdepth; i++) {
      baseType = llvm::PointerType::getUnqual(*cc.TheContext);
    }
  }

  return baseType;
}
