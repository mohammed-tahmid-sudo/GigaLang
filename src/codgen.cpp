#include <ast.h>
#include <llvm-21/llvm/IR/DerivedTypes.h>

CodegenResults IntegerNode::codegen(CodegenContext &cc) {

  return {

      llvm::ConstantInt::get(llvm::IntegerType::getInt32Ty(*cc.Context), value,
                             is_singed),
      nullptr, CreateSystemType(SINGED, 4)};
}
