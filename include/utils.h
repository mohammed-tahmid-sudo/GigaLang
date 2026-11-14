#pragma once

#include <colors.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/Endian.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <strings.h>
#include <types.h>
#include <utility>
#include <vector>

extern bool debug_enabled;

struct CodegenResults {
  llvm::Value *ActualValue;
  llvm::Value *ActualValueButAsAPointer;
  SystemType *Type;
};

struct CodegenContext {
  std::unique_ptr<llvm::LLVMContext> TheContext;
  std::unique_ptr<llvm::IRBuilder<>> Builder;
  std::unique_ptr<llvm::Module> Module;
  std::vector<
      std::unordered_map<std::string, std::pair<llvm::Value *, SystemType>>>
      NamedValuesStack;
  std::unordered_map<std::string, llvm::StructType *> StringToStructs;
  std::unordered_map<llvm::StructType *,
                     std::vector<std::tuple<std::string, size_t, llvm::Type *>>>
      StructsToPair;
  llvm::BasicBlock *BreakBB = nullptr;
  llvm::BasicBlock *ContinueBB = nullptr;

  // Scopes
  void pushScope() { NamedValuesStack.push_back({}); }
  void popScope() { NamedValuesStack.pop_back(); }

  void addVariable(const std::string &name, llvm::Value *value,
                   SystemType &type) {
    NamedValuesStack.back()[name] = {value, type};
  }

  void
  addStruct(const std::string &name, llvm::StructType *Type,
            std::vector<std::tuple<std::string, size_t, llvm::Type *>> Pairs) {
    StringToStructs[name] = Type;
    StructsToPair[Type] = Pairs;
    return;
  }

  std::pair<llvm::Value *, SystemType> lookupVariable(const std::string &name) {
    for (auto it = NamedValuesStack.rbegin(); it != NamedValuesStack.rend();
         ++it)
      if (it->count(name))
        return (*it)[name];
  }

  llvm::StructType *lookupStruct(const std::string &name) {
    auto it = StringToStructs.find(name);
    if (it != StringToStructs.end()) {
      return it->second;
    } else {
      throw std::runtime_error("Unable TO find Struct Called: " + name);
    }
  }

  CodegenContext(const std::string &name)
      : TheContext(std::make_unique<llvm::LLVMContext>()),
        Builder(std::make_unique<llvm::IRBuilder<>>(*TheContext)),
        Module(std::make_unique<llvm::Module>(name, *TheContext)) {}
};

void show_debug(std::string &msg);

llvm::Type *ComputeType(SystemType &type, CodegenContext &cc);
