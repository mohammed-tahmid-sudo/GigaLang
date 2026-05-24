#include <cstddef>
#include <string>


enum  TypeKind {
  Integer,
  Float,
  Boolean,
  Char,
  String,
  Void,

  Array,
  Pointer,
  Function,
  Struct,

  Unknown
};

struct Type {
  TypeKind kind;

  // For arrays/pointers
  Type *base = nullptr;

  // Array info
  size_t arraySize = 0;

  // Pointer info
  size_t pointerDepth = 0;

  // Struct/class name
  std::string name;
};
