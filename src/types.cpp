#include "ast.h"
#include <iostream>
#include <llvm-18/llvm/IR/DerivedTypes.h>
#include <llvm-18/llvm/IR/Metadata.h>
#include <types.h>

llvm::Type *ComputeType(SystemType &type, CodegenContext &cc)
{
  llvm::Type *theLLvmType = nullptr;

  switch (type.kind)
  {
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

  if (type.is_arr)
  {
    theLLvmType = llvm::ArrayType::get(theLLvmType, type.size_arr);
  }

  if (type.is_ptr || type.ptrdepth > 0)
  {
    theLLvmType = llvm::PointerType::get(*cc.TheContext, 0);
  }

  return theLLvmType;
}

void TurnTokenToType(SystemType& type, const Token& token)
{
    switch (token.type)
    {
    case IDENTIFIER:
        type.kind = TypeKind::STRUCTTY;
        type.struct_name = token.value;
        break;

    case TYPES:
    {
        std::string holder = token.value;

        for (char& c : holder)
        {
            c = static_cast<char>(
                std::toupper(static_cast<unsigned char>(c)));
        }

        if (holder == "INTEGER")
        {
            // std::cout << "FOUND INTEGER\n";
            type.kind = TypeKind::INTEGER;
        }
        else if (holder == "FLOAT")
        {
            // std::cout << "FOUND FLOAT\n";
            type.kind = TypeKind::FLOAT;
        }
        else if (holder == "CHAR")
        {
            // std::cout << "FOUND CHAR\n";
            type.kind = TypeKind::CHAR;
        }
        else if (holder == "BOOLEAN")
        {
            // std::cout << "FOUND BOOLEAN\n";
            type.kind = TypeKind::BOOLEAN;
        }
        else if (holder == "VOID")
        {
            // std::cout << "FOUND VOID\n";
            type.kind = TypeKind::VOID;
        }
        else
        {
            throw std::runtime_error(
                "Unknown type name: " + token.value);
        }

        break;
    }

    default:
        throw std::runtime_error("Unexpected token type");
    }
}
