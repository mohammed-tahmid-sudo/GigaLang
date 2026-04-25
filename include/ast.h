#pragma once
#include "lexer.h"
#include <llvm-18/llvm/IR/DerivedTypes.h>
#include <llvm-18/llvm/IR/IRBuilder.h>
#include <llvm-18/llvm/IR/LLVMContext.h>
#include <llvm-18/llvm/IR/Type.h>
#include <llvm-18/llvm/IR/Value.h>
#include <llvm-18/llvm/Support/Endian.h>
#include <memory>
#include <string>
#include <strings.h>
#include <utility>
#include <vector>

struct CodegenResult {
  llvm::Value *value;
  llvm::Type *type;
};

struct VWT {
  llvm::Value *val;
  llvm::Type *type;
  llvm::Type *elementType;
};

struct StructIndex {
  llvm::StructType *TheStruct;
  std::vector<std::pair<std::string, size_t>> index;
  StructIndex(llvm::StructType *thestruct,
              std::vector<std::pair<std::string, size_t>> idx)
      : TheStruct(thestruct), index(idx) {}
};

struct CodegenContext {
  std::unique_ptr<llvm::LLVMContext> TheContext;
  std::unique_ptr<llvm::IRBuilder<>> Builder;
  std::unique_ptr<llvm::Module> Module;
  std::vector<std::unordered_map<std::string, VWT>> NamedValuesStack;

  std::unordered_map<std::string, llvm::StructType *> StructIndexList;
  std::unordered_map<llvm::StructType *,
                     std::vector<std::pair<std::string, size_t>>>
      StructToIndex;

  llvm::BasicBlock *BreakBB = nullptr;
  llvm::BasicBlock *ContinueBB = nullptr;

  void CreateStructWithIndex(const std::string &s, llvm::StructType *StructType,
                             std::vector<std::pair<std::string, size_t>> args) {
    StructIndexList[s] = StructType;
    StructToIndex[StructType] = args;
  }

  void pushScope() { NamedValuesStack.push_back({}); }
  void popScope() { NamedValuesStack.pop_back(); }

  void addVariable(const std::string &name, llvm::Value *value,
                   llvm::Type *Type, llvm::Type *elemenType) {
    NamedValuesStack.back()[name] = VWT{value, Type, elemenType};
  }

  llvm::Value *lookup(const std::string &name) {
    for (auto it = NamedValuesStack.rbegin(); it != NamedValuesStack.rend();
         ++it)
      if (it->count(name))
        return (*it)[name].val;
    return nullptr;
  }

  llvm::Type *lookupType(const std::string &name) {
    for (auto it = NamedValuesStack.rbegin(); it != NamedValuesStack.rend();
         ++it)
      if (it->count(name))
        return (*it)[name].type;
    return nullptr;
  }
  llvm::Type *lookupElementType(const std::string &name) {
    for (auto it = NamedValuesStack.rbegin(); it != NamedValuesStack.rend();
         ++it)
      if (it->count(name))
        return (*it)[name].elementType;
    return nullptr;
  }

  CodegenContext(const std::string &name)
      : TheContext(std::make_unique<llvm::LLVMContext>()),
        Builder(std::make_unique<llvm::IRBuilder<>>(*TheContext)),
        Module(std::make_unique<llvm::Module>(name, *TheContext)) {}
};

llvm::Type *GetPointeeType(Token typeToken, CodegenContext &cc);
llvm::Type *GetTypeVoid(Token type, CodegenContext &cc);
llvm::Type *GetTypeNonVoid(Token type, CodegenContext &cc);

struct ast {
  virtual ~ast() = default;
  virtual std::string repr() = 0;
  virtual CodegenResult codegen(CodegenContext &cc) = 0;
};

struct CharNode : ast {
  char val;

  CharNode(char value) : val(value) {}
  std::string repr() override;
  CodegenResult codegen(CodegenContext &cc) override;
};

struct IntegerNode : ast {
  int val;
  IntegerNode(const int v) : val(v) {}
  std::string repr() override;
  CodegenResult codegen(CodegenContext &cc) override;
};

struct FloatNode : ast {
  float val;
  FloatNode(float v) : val(v) {}
  std::string repr() override;
  CodegenResult codegen(CodegenContext &cc) override;
};

struct BooleanNode : ast {
  bool val;
  BooleanNode(bool v) : val(v) {}
  std::string repr() override;
  CodegenResult codegen(CodegenContext &cc) override;
};

// struct StringNode : ast {
//   std::string val;
//   StringNode(const std::string &v) : val(v) {}
//   std::string repr() override;
//   CodegenResult codegen(CodegenContext &cc) override;
// };

struct VariableDeclareNode : ast {
  std::string name;
  Token Type;
  std::unique_ptr<ast> val;          // can be single value or ArrayLiteralNode
  std::optional<unsigned> arraySize; // new: size if it's an array

  VariableDeclareNode(const std::string &n, std::unique_ptr<ast> v, Token t,
                      std::optional<unsigned> size = 1)
      : name(n), val(std::move(v)), Type(t), arraySize(size) {}

  std::string repr() override;

  CodegenResult codegen(CodegenContext &cc) override;
};

struct AssignmentNode : ast {
  std::unique_ptr<ast> lhs;
  std::unique_ptr<ast> rhs;
  AssignmentNode(std::unique_ptr<ast> n, std::unique_ptr<ast> v)
      : lhs(std::move(n)), rhs(std::move(v)) {}
  std::string repr() override;
  CodegenResult codegen(CodegenContext &cc) override;
};

struct ReturnNode : ast {
  std::unique_ptr<ast> expr;
  ReturnNode(std::unique_ptr<ast> exp) : expr(std::move(exp)) {}
  std::string repr() override;
  CodegenResult codegen(CodegenContext &cc) override;
};
struct CompoundNode : ast {
  std::vector<std::unique_ptr<ast>> blocks;
  CompoundNode(std::vector<std::unique_ptr<ast>> b) : blocks(std::move(b)) {}
  std::string repr() override;
  CodegenResult codegen(CodegenContext &cc) override;
};

struct FunctionNode : ast {
  std::string name;
  std::vector<std::tuple<std::string, llvm::Type *>> args;
  bool isVaridic;
  std::unique_ptr<ast> content;
  Token ReturnType;

  FunctionNode(const std::string &s,
               std::vector<std::tuple<std::string, llvm::Type *>> ars,
               std::unique_ptr<ast> cntnt, Token RetType, bool varidic)
      : name(s), args(ars), content(std::move(cntnt)), ReturnType(RetType),
        isVaridic(varidic) {}

  std::string repr() override;
  CodegenResult codegen(CodegenContext &cc) override;
};

struct VariableReferenceNode : ast {
  std::string Name;

  VariableReferenceNode(const std::string &s) : Name(s) {}
  std::string repr() override;
  CodegenResult codegen(CodegenContext &cc) override;
};

struct WhileNode : ast {
  std::unique_ptr<ast> condition;
  std::unique_ptr<ast> body;

  WhileNode(std::unique_ptr<ast> condtn, std::unique_ptr<ast> bdy)
      : condition(std::move(condtn)), body(std::move(bdy)) {}
  std::string repr() override;
  CodegenResult codegen(CodegenContext &cc) override;
};

struct IfNode : ast {
  std::unique_ptr<ast> condition;
  std::unique_ptr<ast> thenBlock;
  std::unique_ptr<ast> elseBlock;

  IfNode(std::unique_ptr<ast> cond, std::unique_ptr<ast> thenB,
         std::unique_ptr<ast> elseB)
      : condition(std::move(cond)), thenBlock(std::move(thenB)),
        elseBlock(std::move(elseB)) {}

  std::string repr() override;
  CodegenResult codegen(CodegenContext &cc) override;
};

struct BinaryOperationNode : ast {
  std::unique_ptr<ast> Left;
  std::unique_ptr<ast> Right;
  TokenType Type;
  BinaryOperationNode(TokenType tp, std::unique_ptr<ast> LHS,
                      std::unique_ptr<ast> RHS)
      : Type(tp), Left(std::move(LHS)), Right(std::move(RHS)) {}

  std::string repr() override;
  CodegenResult codegen(CodegenContext &cc) override;
};

struct BreakNode : ast {
  std::string repr() override;
  CodegenResult codegen(CodegenContext &cc) override;
};

struct ContinueNode : ast {
  std::string repr() override;
  CodegenResult codegen(CodegenContext &cc) override;
};
struct CallNode : ast {
  std::string name;
  std::vector<std::unique_ptr<ast>> args;

  CallNode(const std::string &s, std::vector<std::unique_ptr<ast>> arg)
      : name(s), args(std::move(arg)) {}
  std::string repr() override;
  CodegenResult codegen(CodegenContext &cc) override;
};

struct ForNode : ast {
  std::unique_ptr<ast> init;      // e.g., i = 0
  std::unique_ptr<ast> condition; // e.g., i < 10
  std::unique_ptr<ast> increment; // e.g., i = i + 1
  std::unique_ptr<ast> body;      // block of statements

  ForNode(std::unique_ptr<ast> init, std::unique_ptr<ast> cond,
          std::unique_ptr<ast> inc, std::unique_ptr<ast> body)
      : init(std::move(init)), condition(std::move(cond)),
        increment(std::move(inc)), body(std::move(body)) {}

  std::string repr() override;
  CodegenResult codegen(CodegenContext &cc) override;
};

struct ArrayLiteralNode : ast {
  llvm::Type *ElementType;
  std::vector<std::unique_ptr<ast>> Elements;

  ArrayLiteralNode(llvm::Type *elemType,
                   std::vector<std::unique_ptr<ast>> elements)
      : ElementType(elemType), Elements(std::move(elements)) {}

  std::string repr() override;
  CodegenResult codegen(CodegenContext &cc) override;
};

struct ArrayAccessNode : ast {
  std::string arrayName;
  std::unique_ptr<ast> indexExpr;

  ArrayAccessNode(const std::string &name, std::unique_ptr<ast> index)
      : arrayName(name), indexExpr(std::move(index)) {}

  std::string repr() override;
  CodegenResult codegen(CodegenContext &cc) override;
};

struct ArrayAssignNode : ast {
  std::string name;           // array name
  std::unique_ptr<ast> index; // index expression
  std::unique_ptr<ast> value; // value to assign

  ArrayAssignNode(const std::string &n, std::unique_ptr<ast> idx,
                  std::unique_ptr<ast> val)
      : name(n), index(std::move(idx)), value(std::move(val)) {}

  std::string repr() override;

  CodegenResult codegen(CodegenContext &cc) override;
};

struct SizeOfNode : ast {
  std::unique_ptr<ast> val;

  SizeOfNode(std::unique_ptr<ast> valval) : val(std::move(valval)) {}
  std::string repr() override;

  CodegenResult codegen(CodegenContext &cc) override;
};

struct SyscallNode : ast {
  int name;
  std::vector<std::unique_ptr<ast>> args;
  SyscallNode(int syscall_name, std::vector<std::unique_ptr<ast>> syscall_args)
      : name(syscall_name), args(std::move(syscall_args)) {}
  std::string repr() override { return "SYSCALLNODE"; }

  CodegenResult codegen(CodegenContext &cc) override;
};

struct PointerReferenceNode : ast {
  std::string name;
  PointerReferenceNode(const std::string &s) : name(s) {}

  std::string repr() override;

  CodegenResult codegen(CodegenContext &cc) override;
};

struct PointerDeReferenceAssingNode : ast {
  std::string name;
  std::unique_ptr<ast> val;
  std::unique_ptr<ast> index;

  PointerDeReferenceAssingNode(const std::string &n, std::unique_ptr<ast> v,
                               std::unique_ptr<ast> i)
      : name(n), val(std::move(v)), index(std::move(i)) {}

  std::string repr() override;

  CodegenResult codegen(CodegenContext &cc) override;
};

struct DeReferenceNode : ast {
  std::string name;
  std::unique_ptr<ast> index;

  DeReferenceNode(const std::string &n, std::unique_ptr<ast> idx)
      : name(n), index(std::move(idx)) {}
  std::string repr() override { return "PointerDeReferenceNode"; }

  CodegenResult codegen(CodegenContext &cc) override;
};

struct CastNode : ast {
  std::unique_ptr<ast> Value;
  llvm::Type *targetType;

  CastNode(std::unique_ptr<ast> V, llvm::Type *type)
      : Value(std::move(V)), targetType(type) {}
  std::string repr() override { return "CastNode"; }

  CodegenResult codegen(CodegenContext &cc) override;
};

struct StructCreateNode : ast {
  std::unordered_map<std::string, llvm::Type *> types;
  std::string name;
  StructCreateNode(const std::string &s,
                   std::unordered_map<std::string, llvm::Type *> tps)
      : name(s), types(std::move(tps)) {}
  std::string repr() override { return "CastNode"; }

  CodegenResult codegen(CodegenContext &cc) override;
};

struct FieldAccessNode : ast {
  std::unique_ptr<ast> Base;
  std::string name;
  FieldAccessNode(std::unique_ptr<ast> bs, const std::string &s)
      : Base(std::move(bs)), name(s) {}

  std::string repr() override { return "FIELDACCESNODE"; }
  CodegenResult codegen(CodegenContext &cc) override;
};
