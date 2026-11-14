#include "ast.h"
#include <iostream>
#include <llvm-21/llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Metadata.h>
#include <string>
#include <types.h>


void TurnTokenToType(SystemType &type, const Token &token) {
  switch (token.type) {
  case IDENTIFIER:
    type.kind = TypeKind::STRUCTS;
    type.struct_name = token.value;
    break;

  case TYPES: {
    std::string holder = token.value;

    for (char &c : holder) {
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }

    if (holder[0] == 'U') {
      type.kind = TypeKind::UNSINGED;
      type.size = std::stoi(holder.substr(1));

    } else if (holder[0] == 'S') {
      type.kind = TypeKind::SINGED;
      type.size = std::stoi(holder.substr(1));
    } else if (holder[0] == 'F') {
      type.kind = TypeKind::FLOAT;
      type.size = std::stoi(holder.substr(1));

    } else if (holder == "VOID") {
      type.kind = TypeKind::VOID;
    } else {
      throw std::runtime_error("Unknown type name: " + token.value);
    }

    break;
  }

  default:
    throw std::runtime_error("Unexpected token type");
  }
}
