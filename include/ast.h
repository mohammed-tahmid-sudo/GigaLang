#pragma once

#include <memory>
#include <utils.h>

class ast {
public:
  virtual ~ast() = default;
  virtual CodegenResults codegen(CodegenContext &cc) = 0;
};

struct IntegerNode : public ast {
  int value;
  bool is_singed = true;

  IntegerNode(int val, bool isS) : value(val), is_singed(isS) {}
  CodegenResults codegen(CodegenContext &cc) override;
};
