#pragma once

#include <llvm-21/llvm/IR/IRBuilder.h>
#include <llvm-21/llvm/IR/LLVMContext.h>
#include <llvm-21/llvm/IR/Module.h>
#include <llvm-21/llvm/IR/Type.h>
#include <llvm-21/llvm/IR/Value.h>
#include <memory>
#include <types.h>
#include <utility>

struct CodegenContext {

  std::unique_ptr<llvm::LLVMContext> Context;
  std::unique_ptr<llvm::IRBuilder<>> Builder;
  std::unique_ptr<llvm::Module> Module;

  CodegenContext()
      : Context(std::make_unique<llvm::LLVMContext>()),
        Builder(std::make_unique<llvm::IRBuilder<>>(*Context)),
        Module(std::make_unique<llvm::Module>("GigaLang", *Context)) {}
};

struct CodegenResults {
  llvm::Value *ActualValue;
  llvm::Value *ActualValueButNotAsPointer;
  std::unique_ptr<SystemType> Type;
};

std::unique_ptr<SystemType> CreateSystemType(TypeKind kind = SINGED,
                                             size_t size = 1);
