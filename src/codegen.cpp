#include "lexer.h"
#include "llvm/IR/InlineAsm.h"
#include <alloca.h>
#include <ast.h>
#include <cctype>
#include <cstddef>
#include <iostream>
#include <llvm-18/llvm/ADT/ArrayRef.h>
#include <llvm-18/llvm/ADT/STLExtras.h>
#include <llvm-18/llvm/IR/BasicBlock.h>
#include <llvm-18/llvm/IR/Constant.h>
#include <llvm-18/llvm/IR/Constants.h>
#include <llvm-18/llvm/IR/DerivedTypes.h>
#include <llvm-18/llvm/IR/DiagnosticHandler.h>
#include <llvm-18/llvm/IR/FMF.h>
#include <llvm-18/llvm/IR/Function.h>
#include <llvm-18/llvm/IR/Instructions.h>
#include <llvm-18/llvm/IR/Intrinsics.h>
#include <llvm-18/llvm/IR/LLVMContext.h>
#include <llvm-18/llvm/IR/Metadata.h>
#include <llvm-18/llvm/IR/Type.h>
#include <llvm-18/llvm/IR/Value.h>
#include <llvm-18/llvm/IR/Verifier.h>
#include <llvm-18/llvm/Support/Casting.h>
#include <llvm-18/llvm/Support/TypeName.h>
#include <llvm-18/llvm/Support/raw_ostream.h>
#include <memory>
#include <stdexcept>
#include <strings.h>
#include <vector>

llvm::Type *GetPointeeType(Token typeToken, CodegenContext &cc) {
  std::string t = typeToken.value;
  for (char &c : t)
    c = toupper(c);

  if (t.size() > 7 && t.substr(t.size() - 7) == "POINTER") {
    Token baseToken;
    baseToken.value = t.substr(0, t.size() - 7); // strip "POINTER"
    return GetTypeNonVoid(baseToken, cc); // "CHAR" -> i8, "INTEGER" -> i32
  }
  return nullptr;
}

llvm::Type *GetTypeNonVoid(Token type, CodegenContext &cc) {
  std::string t = type.value;

  for (char &c : t)
    c = toupper(c);

  if (t.size() > 7 && t.substr(t.size() - 7) == "POINTER") {
    Token baseTypeToken;
    baseTypeToken.value = t.substr(0, t.size() - 7);
    llvm::Type *baseType = GetTypeNonVoid(baseTypeToken, cc);
    return llvm::PointerType::get(baseType, 0);
  }

  if (type.type == IDENTIFIER) {
    return cc.lookupStruct(type.value);
  }

  if (t == "INTEGER") {
    return llvm::Type::getInt32Ty(*cc.TheContext);
  } else if (t == "FLOAT") {
    return llvm::Type::getFloatTy(*cc.TheContext);
  } else if (t == "STRING") {
    return llvm::Type::getInt8Ty(*cc.TheContext);
  } else if (t == "BOOLEAN") {
    return llvm::Type::getInt1Ty(*cc.TheContext);
  } else if (t == "CHAR") {
    return llvm::Type::getInt8Ty(*cc.TheContext);
  }

  throw std::runtime_error("Invalid Type: " + type.value);
  return nullptr;
}

llvm::Type *GetTypeVoid(Token type, CodegenContext &cc) {
  Token holder = type;
  for (char &c : holder.value)
    c = toupper(c);

  if (holder.value == "VOID")
    return llvm::Type::getVoidTy(*cc.TheContext);

  return GetTypeNonVoid(type, cc);
}

CodegenResults CharNode::codegen(CodegenContext &cc) {
  return {
      llvm::ConstantInt::get(llvm::Type::getInt8Ty(*cc.TheContext), val, false),
      nullptr, llvm::Type::getInt8Ty(*cc.TheContext), nullptr};
}

CodegenResults IntegerNode::codegen(CodegenContext &cc) {
  return {
      llvm::ConstantInt::get(llvm::Type::getInt32Ty(*cc.TheContext), val, true),
      nullptr, llvm::Type::getInt32Ty(*cc.TheContext), nullptr};
}

CodegenResults FloatNode::codegen(CodegenContext &cc) {

  return {llvm::ConstantFP::get(llvm::Type::getFloatTy(*cc.TheContext), val),
          nullptr, llvm::Type::getFloatTy(*cc.TheContext), nullptr};
}

CodegenResults BooleanNode::codegen(CodegenContext &cc) {
  return {
      llvm::ConstantInt::get(llvm::Type::getInt1Ty(*cc.TheContext), val, true),
      nullptr, llvm::Type::getInt1Ty(*cc.TheContext), nullptr};
}

CodegenResults VariableDeclareNode::codegen(CodegenContext &cc) {
  llvm::Type *elementType = GetTypeNonVoid(Type, cc);
  llvm::AllocaInst *alloca = nullptr;
  llvm::Type *finalType = elementType;

  if (arraySize.has_value()) {
    finalType = llvm::ArrayType::get(elementType, arraySize.value());
    alloca = cc.Builder->CreateAlloca(finalType, nullptr, name);

    if (val) {
      CodegenResults initRes = val->codegen(cc);
      cc.Builder->CreateStore(initRes.ActualValue, alloca);
    }
  } else {
    alloca = cc.Builder->CreateAlloca(elementType, nullptr, name);
    if (val) {
      CodegenResults initRes = val->codegen(cc);
      cc.Builder->CreateStore(initRes.ActualValue, alloca);
    }
  }

  llvm::Type *elemType = GetTypeNonVoid(Type, cc);

  if (arraySize.has_value()) {
    finalType = llvm::ArrayType::get(elemType, arraySize.value());
  } else {
    finalType = elemType;
  }

  cc.addVariable(name, alloca, finalType, elemType);
  return {
      cc.Builder->CreateLoad(finalType, alloca), // ActualValue (the data)
      alloca,                    // ActualValueButAsAPointer (the address)
      finalType->getPointerTo(), // ActualType (pointer type)
      finalType                  // ActualTypeButNotThePointer
  };
}

CodegenResults AssignmentNode::codegen(CodegenContext &cc) {

  CodegenResults LHS = lhs->codegen(cc);
  CodegenResults RHS = rhs->codegen(cc);

  if (!LHS.ActualValueButAsAPointer || !RHS.ActualValue) {
    std::cerr << "The Problem sis at AssignmentNode" << std::endl;
  }

  return {
      cc.Builder->CreateStore(RHS.ActualValue, LHS.ActualValueButAsAPointer),
      nullptr, LHS.ActualType, LHS.ActualTypeButNotThePointer};
}

CodegenResults ReturnNode::codegen(CodegenContext &cc) {
  if (expr) {
    CodegenResults retVal = expr->codegen(cc);
    return {cc.Builder->CreateRet(retVal.ActualValue),
            retVal.ActualValueButAsAPointer, retVal.ActualType,
            retVal.ActualTypeButNotThePointer};
  } else {
    return {cc.Builder->CreateRetVoid(), nullptr, nullptr, nullptr};
  }
}

CodegenResults CompoundNode::codegen(CodegenContext &cc) {
  CodegenResults last = {nullptr, nullptr, nullptr, nullptr};

  cc.pushScope();

  for (auto &stmt : blocks) {
    if (!stmt)
      continue;

    if (cc.Builder->GetInsertBlock()->getTerminator())
      break;

    last = stmt->codegen(cc);

    if (cc.Builder->GetInsertBlock()->getTerminator())
      break;
  }

  cc.popScope();
  return last;
}

CodegenResults FunctionNode::codegen(CodegenContext &cc) {
  std::vector<llvm::Type *> argTypes;
  for (auto &a : args)
    argTypes.push_back(std::get<1>(a)); // was a.second

  llvm::Type *retTy = GetTypeVoid(ReturnType, cc);
  auto *FT = llvm::FunctionType::get(retTy, argTypes, isVaridic);
  auto *Fn = llvm::Function::Create(FT, llvm::Function::ExternalLinkage, name,
                                    cc.Module.get());

  auto *BB = llvm::BasicBlock::Create(*cc.TheContext, "entry", Fn);
  cc.Builder->SetInsertPoint(BB);
  cc.pushScope();

  unsigned i = 0;
  for (auto &arg : Fn->args()) {
    const auto &argName = std::get<0>(args[i]);      // was args[i].first
    llvm::Type *declaredType = std::get<1>(args[i]); // was args[i].second
    i++;

    arg.setName(argName);
    llvm::Type *argType = arg.getType();
    auto *alloca = cc.Builder->CreateAlloca(argType, nullptr, argName);
    cc.Builder->CreateStore(&arg, alloca);

    llvm::Type *pointeeType = nullptr;
    if (argType->isPointerTy()) {
      auto &ctx = *cc.TheContext;
      if (declaredType == llvm::PointerType::get(llvm::Type::getInt8Ty(ctx), 0))
        pointeeType = llvm::Type::getInt8Ty(ctx);
      else if (declaredType ==
               llvm::PointerType::get(llvm::Type::getInt32Ty(ctx), 0))
        pointeeType = llvm::Type::getInt32Ty(ctx);
      else if (declaredType ==
               llvm::PointerType::get(llvm::Type::getFloatTy(ctx), 0))
        pointeeType = llvm::Type::getFloatTy(ctx);
      else if (declaredType ==
               llvm::PointerType::get(llvm::Type::getInt1Ty(ctx), 0))
        pointeeType = llvm::Type::getInt1Ty(ctx);
    }

    cc.addVariable(argName, alloca, argType, pointeeType);
  }

  CodegenResults retVal = content->codegen(cc);

  llvm::BasicBlock *currentBB = cc.Builder->GetInsertBlock();
  if (!currentBB->getTerminator()) {
    if (retTy->isVoidTy()) {
      cc.Builder->CreateRetVoid();
    } else {
      if (!retVal.ActualValue) {
        Fn->eraseFromParent();
        cc.popScope();
        return {nullptr, nullptr, nullptr, nullptr};
      }
      cc.Builder->CreateRet(retVal.ActualValue);
    }
  }

  llvm::verifyFunction(*Fn);
  cc.popScope();
  return {Fn, nullptr, Fn->getType(), FT};
}

CodegenResults VariableReferenceNode::codegen(CodegenContext &cc) {
  VWT ptr = cc.lookupVariable(Name); // pointer

  if (!ptr.val || !ptr.type) {
    throw std::runtime_error("VariableReferenceNode");
  }

  // if (!ptr.val){
  // auto it = cc.StringToStructs.find(Name);
  // if (it!= cc.StringToStructs.end()) {
  // ptr.type = it->second;
  // }
  // }

  return {cc.Builder->CreateLoad(ptr.type, ptr.val, Name), ptr.val, ptr.type,
          ptr.elementType};
}

CodegenResults WhileNode::codegen(CodegenContext &cc) {
  llvm::Function *F = cc.Builder->GetInsertBlock()->getParent();
  llvm::LLVMContext &Ctx = *cc.TheContext;

  llvm::BasicBlock *condBB = llvm::BasicBlock::Create(Ctx, "while.cond", F);
  llvm::BasicBlock *bodyBB = llvm::BasicBlock::Create(Ctx, "while.body", F);
  llvm::BasicBlock *afterBB = llvm::BasicBlock::Create(Ctx, "while.end", F);

  cc.Builder->CreateBr(condBB);

  cc.Builder->SetInsertPoint(condBB);
  CodegenResults cond = condition->codegen(cc);
  if (!cond.ActualValue)
    return {nullptr, nullptr, nullptr, nullptr};

  llvm::Value *condVal = cond.ActualValue;

  // force i1
  if (!cond.ActualTypeButNotThePointer->isIntegerTy(1)) {
    condVal = cc.Builder->CreateICmpNE(
        condVal, llvm::ConstantInt::get(condVal->getType(), 0),
        "while.cond.to.i1");
  }

  cc.Builder->CreateCondBr(condVal, bodyBB, afterBB);

  cc.Builder->SetInsertPoint(bodyBB);

  llvm::BasicBlock *oldBreak = cc.BreakBB;
  llvm::BasicBlock *oldCont = cc.ContinueBB;

  cc.BreakBB = afterBB;
  cc.ContinueBB = condBB;

  if (!body->codegen(cc).ActualValue && body != nullptr) {
    cc.BreakBB = oldBreak;
    cc.ContinueBB = oldCont;
    return {nullptr, nullptr, nullptr, nullptr};
  }

  cc.BreakBB = oldBreak;
  cc.ContinueBB = oldCont;

  if (!cc.Builder->GetInsertBlock()->getTerminator())
    cc.Builder->CreateBr(condBB);

  cc.Builder->SetInsertPoint(afterBB);

  return {nullptr, nullptr, nullptr, nullptr};
}

CodegenResults IfNode::codegen(CodegenContext &cc) {
  CodegenResults condR = condition->codegen(cc);
  if (!condR.ActualValue)
    return {nullptr, nullptr, nullptr, nullptr};

  llvm::Value *condV = condR.ActualValue;

  // force i1
  if (!condR.ActualTypeButNotThePointer->isIntegerTy(1)) {
    condV = cc.Builder->CreateICmpNE(
        condV, llvm::ConstantInt::get(condV->getType(), 0), "ifcond");
  }

  llvm::Function *func = cc.Builder->GetInsertBlock()->getParent();

  llvm::BasicBlock *thenBB =
      llvm::BasicBlock::Create(*cc.TheContext, "then", func);

  llvm::BasicBlock *elseBB =
      elseBlock ? llvm::BasicBlock::Create(*cc.TheContext, "else", func)
                : nullptr;

  llvm::BasicBlock *mergeBB =
      llvm::BasicBlock::Create(*cc.TheContext, "ifcont", func);

  if (elseBB)
    cc.Builder->CreateCondBr(condV, thenBB, elseBB);
  else
    cc.Builder->CreateCondBr(condV, thenBB, mergeBB);

  // THEN
  cc.Builder->SetInsertPoint(thenBB);
  cc.pushScope();
  CodegenResults thenR = thenBlock->codegen(cc);
  cc.popScope();

  if (!cc.Builder->GetInsertBlock()->getTerminator())
    cc.Builder->CreateBr(mergeBB);

  // ELSE
  if (elseBB) {
    cc.Builder->SetInsertPoint(elseBB);
    cc.pushScope();
    CodegenResults elseR = elseBlock->codegen(cc);
    cc.popScope();

    if (!cc.Builder->GetInsertBlock()->getTerminator())
      cc.Builder->CreateBr(mergeBB);
  }

  // MERGE
  cc.Builder->SetInsertPoint(mergeBB);

  return {nullptr, nullptr, nullptr, nullptr};
}

CodegenResults BinaryOperationNode::codegen(CodegenContext &cc) {
  CodegenResults L = Left->codegen(cc);
  CodegenResults R = Right->codegen(cc);

  if (!L.ActualValue || !R.ActualValue)
    throw std::runtime_error("null operand in binary operation");

  llvm::Value *LHS = L.ActualValue;
  llvm::Value *RHS = R.ActualValue;

  llvm::Type *LT = LHS->getType();
  llvm::Type *RT = RHS->getType();

  switch (Type) {

  case TokenType::PLUS:
  case TokenType::MINUS:
  case TokenType::STAR:
  case TokenType::SLASH: {

    auto *i32 = llvm::Type::getInt32Ty(*cc.TheContext);

    if (LT->isIntegerTy(1))
      LHS = cc.Builder->CreateIntCast(LHS, i32, true);
    if (RT->isIntegerTy(1))
      RHS = cc.Builder->CreateIntCast(RHS, i32, true);

    LT = LHS->getType();
    RT = RHS->getType();

    if (LT != RT) {
      if (LT->isIntegerTy() && RT->isIntegerTy()) {
        RHS = cc.Builder->CreateIntCast(RHS, LT, true);
      } else {
        throw std::runtime_error(
            "Cannot perform arithmetic on incompatible types");
      }
    }

    llvm::Value *result = nullptr;

    if (Type == TokenType::PLUS)
      result = cc.Builder->CreateAdd(LHS, RHS, "addtmp");
    else if (Type == TokenType::MINUS)
      result = cc.Builder->CreateSub(LHS, RHS, "subtmp");
    else if (Type == TokenType::STAR)
      result = cc.Builder->CreateMul(LHS, RHS, "multmp");
    else
      result = cc.Builder->CreateSDiv(LHS, RHS, "divtmp");

    return {result, nullptr, LT, LT};
  }

  case TokenType::EQEQ:
  case TokenType::NOTEQ:
  case TokenType::GTE:
  case TokenType::LTE:
  case TokenType::GT:
  case TokenType::LT: {

    if (LT != RT) {
      if (LT->isIntegerTy() && RT->isIntegerTy()) {
        RHS = cc.Builder->CreateIntCast(RHS, LT, true);
      } else {
        throw std::runtime_error("Cannot compare incompatible types");
      }
    }

    llvm::Value *result = nullptr;

    switch (Type) {
    case TokenType::EQEQ:
      result = cc.Builder->CreateICmpEQ(LHS, RHS, "eqtmp");
      break;
    case TokenType::NOTEQ:
      result = cc.Builder->CreateICmpNE(LHS, RHS, "netmp");
      break;
    case TokenType::GTE:
      result = cc.Builder->CreateICmpSGE(LHS, RHS, "gtetmp");
      break;
    case TokenType::LTE:
      result = cc.Builder->CreateICmpSLE(LHS, RHS, "ltetmp");
      break;
    case TokenType::GT:
      result = cc.Builder->CreateICmpSGT(LHS, RHS, "gttmp");
      break;
    case TokenType::LT:
      result = cc.Builder->CreateICmpSLT(LHS, RHS, "lttmp");
      break;
    default:
      break;
    }

    return {result, nullptr, result->getType(), result->getType()};
  }

  case TokenType::AND: {
    if (!LT->isIntegerTy(1))
      LHS = cc.Builder->CreateICmpNE(LHS, llvm::ConstantInt::get(LT, 0),
                                     "lhsbool");

    if (!RT->isIntegerTy(1))
      RHS = cc.Builder->CreateICmpNE(RHS, llvm::ConstantInt::get(RT, 0),
                                     "rhsbool");

    llvm::Value *result = cc.Builder->CreateAnd(LHS, RHS, "andtmp");

    return {result, nullptr, llvm::Type::getInt1Ty(*cc.TheContext),
            llvm::Type::getInt1Ty(*cc.TheContext)};
  }

  default:
    throw std::runtime_error("Unknown binary operator");
  }
}

CodegenResults BreakNode::codegen(CodegenContext &cc) {
  if (!cc.BreakBB) {
    std::cerr << "Error: 'break' not inside a loop.\n";
    return {nullptr, nullptr, nullptr, nullptr};
  }
  return {cc.Builder->CreateBr(cc.BreakBB), nullptr, nullptr, nullptr};
}

CodegenResults CallNode::codegen(CodegenContext &cc) {
  llvm::Function *callee = cc.Module->getFunction(name);
  if (!callee)
    return {nullptr, nullptr, nullptr, nullptr};

  if (callee->arg_size() != args.size())
    throw std::runtime_error("Argument count mismatch in function call");

  std::vector<llvm::Value *> argVals;

  auto it = callee->arg_begin();

  for (auto &arg : args) {
    CodegenResults r = arg->codegen(cc);
    if (!r.ActualValue)
      return {nullptr, nullptr, nullptr, nullptr};

    llvm::Value *v = r.ActualValue;

    // optional: basic type alignment (important for safety)
    llvm::Type *expected = it->getType();
    if (v->getType() != expected) {
      if (v->getType()->isIntegerTy() && expected->isIntegerTy()) {
        v = cc.Builder->CreateIntCast(v, expected, true);
      } else {
        throw std::runtime_error("Type mismatch in function call argument");
      }
    }

    argVals.push_back(v);
    ++it;
  }

  llvm::Value *call = cc.Builder->CreateCall(callee, argVals);

  return {call, nullptr, callee->getReturnType(), callee->getReturnType()};
}

CodegenResults ContinueNode::codegen(CodegenContext &cc) {

  if (!cc.ContinueBB) {
    std::cerr << "Error: 'continue' not inside a loop.\n";
    return {nullptr, nullptr, nullptr, nullptr};
  }

  cc.Builder->CreateBr(cc.ContinueBB);

  return {nullptr, nullptr, nullptr, nullptr};
}

CodegenResults ForNode::codegen(CodegenContext &cc) {
  llvm::Function *function = cc.Builder->GetInsertBlock()->getParent();

  if (init) {
    init->codegen(cc);
  }

  llvm::BasicBlock *loopCondBB =
      llvm::BasicBlock::Create(*cc.TheContext, "loopcond", function);

  llvm::BasicBlock *loopBodyBB =
      llvm::BasicBlock::Create(*cc.TheContext, "loopbody", function);

  llvm::BasicBlock *loopEndBB =
      llvm::BasicBlock::Create(*cc.TheContext, "loopend", function);

  // set loop context
  llvm::BasicBlock *oldBreak = cc.BreakBB;
  llvm::BasicBlock *oldCont = cc.ContinueBB;

  cc.BreakBB = loopEndBB;
  cc.ContinueBB = loopCondBB;

  cc.Builder->CreateBr(loopCondBB);

  // CONDITION
  cc.Builder->SetInsertPoint(loopCondBB);

  CodegenResults condR = condition->codegen(cc);
  if (!condR.ActualValue)
    return {nullptr, nullptr, nullptr, nullptr};

  llvm::Value *condValue = condR.ActualValue;

  // force i1
  if (!condR.ActualTypeButNotThePointer->isIntegerTy(1)) {
    condValue = cc.Builder->CreateICmpNE(
        condValue, llvm::ConstantInt::get(condValue->getType(), 0), "forcond");
  }

  cc.Builder->CreateCondBr(condValue, loopBodyBB, loopEndBB);

  // BODY
  cc.Builder->SetInsertPoint(loopBodyBB);

  if (body)
    body->codegen(cc);

  // INCREMENT
  if (increment)
    increment->codegen(cc);

  // go back to condition
  if (!cc.Builder->GetInsertBlock()->getTerminator())
    cc.Builder->CreateBr(loopCondBB);

  // restore loop context
  cc.BreakBB = oldBreak;
  cc.ContinueBB = oldCont;

  cc.Builder->SetInsertPoint(loopEndBB);

  return {nullptr, nullptr, nullptr, nullptr};
}

CodegenResults ArrayLiteralNode::codegen(CodegenContext &cc) {
  if (Elements.empty()) {
    auto *EmptyTy =
        llvm::ArrayType::get(llvm::Type::getInt8Ty(*cc.TheContext), 0);
    return {llvm::ConstantArray::get(EmptyTy, {})};
  }

  std::vector<llvm::Constant *> ConstantValues;
  for (auto &x : Elements) {
    auto results = x->codegen(cc);
    auto *C = llvm::dyn_cast<llvm::Constant>(results.ActualValue);

    if (!C) {
      throw std::runtime_error("SOMETHING IS WRONG AT ARRAY LITERLANODE");
    }
    ConstantValues.push_back(C);
  }

  // Use the first element's type as the master type
  llvm::Type *ElementType = ConstantValues[0]->getType();

  // OPTIONAL: Verify all elements match the first element's type
  for (auto *V : ConstantValues) {
    if (V->getType() != ElementType) {
      throw std::runtime_error("SOMETHING IS WRONG AT ARRAY LITERLANODE");
    }
  }

  llvm::ArrayType *ATy =
      llvm::ArrayType::get(ElementType, ConstantValues.size());
  return {llvm::ConstantArray::get(ATy, ConstantValues), nullptr, ATy, nullptr};
}

CodegenResults SizeOfNode::codegen(CodegenContext &cc) {
  std::cerr << "SIZEOF DOES NOT WORK FOR NOT" << std::endl;
  return {nullptr, nullptr, nullptr, nullptr};
}

CodegenResults ArrayAccessNode::codegen(CodegenContext &cc) {
  VWT array = cc.lookupVariable(arrayName);
  CodegenResults idx = indexExpr->codegen(cc);

  llvm::Value *elementPtr = nullptr;

  if (array.type->isArrayTy()) {

    llvm::Value *indices[] = {cc.Builder->getInt32(0), idx.ActualValue};

    elementPtr = cc.Builder->CreateInBoundsGEP(array.type, array.val, indices);

  } else if (array.type->isPointerTy()) {

    llvm::Value *realPtr = cc.Builder->CreateLoad(array.type, array.val);

    elementPtr = cc.Builder->CreateInBoundsGEP(array.elementType, realPtr,
                                               idx.ActualValue);
  }

  return {cc.Builder->CreateLoad(array.elementType, elementPtr), elementPtr,
          array.elementType, array.elementType};
}

CodegenResults SyscallNode::codegen(CodegenContext &cc) {
  if (!cc.TheContext || !cc.Builder)
    throw std::runtime_error("SyscallNode: invalid codegen context");

  llvm::Type *i64Ty = llvm::Type::getInt64Ty(*cc.TheContext);
  std::vector<llvm::Value *> llvm_args;
  llvm_args.reserve(args.size());

  for (size_t i = 0; i < args.size(); i++) {
    auto &arg = args[i];

    if (!arg)
      throw std::runtime_error("SyscallNode: null argument at index " +
                               std::to_string(i));

    CodegenResults v = arg->codegen(cc);

    if (!v.ActualValue)
      throw std::runtime_error(
          "SyscallNode: failed to generate code for argument " +
          std::to_string(i));

    llvm_args.push_back(v.ActualValue);
  }

  while (llvm_args.size() < 6)
    llvm_args.push_back(llvm::ConstantInt::get(i64Ty, 0));

  if (llvm_args.size() > 6)
    throw std::runtime_error("SyscallNode: too many arguments (max 6)");

  if (name < 0)
    throw std::runtime_error("SyscallNode: invalid syscall number");

  llvm::Value *syscall_num = llvm::ConstantInt::get(i64Ty, name);

  std::vector<llvm::Value *> final_args = {syscall_num};
  final_args.insert(final_args.end(), llvm_args.begin(), llvm_args.begin() + 6);

  llvm::FunctionType *ft = llvm::FunctionType::get(
      i64Ty, std::vector<llvm::Type *>(7, i64Ty), false);

  if (!ft)
    throw std::runtime_error("SyscallNode: failed to create function type");

  llvm::InlineAsm *asmSyscall = llvm::InlineAsm::get(
      ft, "syscall",
      "={rax},{rax},{rdi},{rsi},{rdx},{r10},{r8},{r9},~{rcx},~{r11},~{memory}",
      true);

  if (!asmSyscall)
    throw std::runtime_error("SyscallNode: failed to create inline asm");

  llvm::CallInst *call = cc.Builder->CreateCall(asmSyscall, final_args);

  if (!call)
    throw std::runtime_error("SyscallNode: failed to emit call instruction");

  return {call, nullptr, nullptr, nullptr};
}

CodegenResults PointerReferenceNode::codegen(CodegenContext &cc) {
  VWT var = cc.lookupVariable(name);

  if (!var.val) {
    throw std::runtime_error("CANNOT FIND VALUE " + name);
  }
  return {var.val, var.val, var.type, var.elementType};
}

CodegenResults DeReferenceNode::codegen(CodegenContext &cc) {
  VWT var = cc.lookupVariable(name);
  if (!var.val) {
    llvm::errs() << "Unknown variable '" << name << "'\n";
    return {nullptr, nullptr, nullptr, nullptr};
  }

  if (nullptr == var.type) {
    throw std::runtime_error("NullPointer, Baby");
  }
  if (!var.type || !var.type->isPointerTy()) {
    llvm::errs() << "'" << name << "' is not a pointer\n";
    return {nullptr, nullptr, nullptr, nullptr};
  }

  llvm::Value *ptrVal =
      cc.Builder->CreateLoad(var.type, var.val, name + "_ptr");

  // If there's an index, apply GEP before loading
  if (index) {
    CodegenResults idx = index->codegen(cc);
    ptrVal = cc.Builder->CreateGEP(var.elementType, ptrVal, {idx.ActualValue},
                                   "ptr_elem");
  }
  return {cc.Builder->CreateLoad(var.elementType, ptrVal, "deref_" + name),
          var.val, var.type, var.elementType};
}

llvm::Value *castValue(llvm::IRBuilder<> &builder, llvm::Value *val,
                       llvm::Type *targetType, bool isSigned) {
  llvm::Type *srcType = val->getType();

  if (srcType == targetType)
    return val;

  // ===== Integer ↔ Integer (covers CHAR <-> INT, BOOL, etc) =====
  if (srcType->isIntegerTy() && targetType->isIntegerTy()) {
    return builder.CreateIntCast(val, targetType, isSigned);
  }

  // ===== Integer → Float =====
  if (srcType->isIntegerTy() && targetType->isFloatingPointTy()) {
    return isSigned ? builder.CreateSIToFP(val, targetType)
                    : builder.CreateUIToFP(val, targetType);
  }

  // ===== Float → Integer (covers float -> char too) =====
  if (srcType->isFloatingPointTy() && targetType->isIntegerTy()) {
    return isSigned ? builder.CreateFPToSI(val, targetType)
                    : builder.CreateFPToUI(val, targetType);
  }

  // ===== Float ↔ Float =====
  if (srcType->isFloatingPointTy() && targetType->isFloatingPointTy()) {
    return builder.CreateFPCast(val, targetType);
  }

  // ===== Pointer stuff (optional for now) =====
  if (srcType->isPointerTy() && targetType->isPointerTy()) {
    return builder.CreateBitCast(val, targetType);
  }

  if (srcType->isPointerTy() && targetType->isIntegerTy()) {
    return builder.CreatePtrToInt(val, targetType);
  }

  if (srcType->isIntegerTy() && targetType->isPointerTy()) {
    return builder.CreateIntToPtr(val, targetType);
  }

  llvm::errs() << "Unsupported cast\n";
  return nullptr;
}

CodegenResults CastNode::codegen(CodegenContext &cc) {
  CodegenResults v = Value->codegen(cc);
  return {castValue(*cc.Builder, v.ActualValue, targetType, true), nullptr,
          v.ActualType, v.ActualTypeButNotThePointer};
}

CodegenResults StructCreateNode::codegen(CodegenContext &cc) {
  llvm::StructType *TheStruct = llvm::StructType::create(*cc.TheContext, name);
  std::vector<llvm::Type *> fieldTypes;
  fieldTypes.reserve(types.size());

  std::vector<std::pair<std::string, size_t>> indexs;
  size_t i = 0;
  for (const auto &p : types) {
    fieldTypes.push_back(p.second);
    indexs.push_back({p.first, i});
    i++;
  }

  TheStruct->setBody(fieldTypes);

  // auto idx = std::make_unique<StructIndex>(TheStruct, indexs);

  // cc.StructIndexList.emplace(name, std::move(idx));
  cc.addStruct(name, TheStruct, indexs);

  return {nullptr, nullptr, nullptr, nullptr};
}

CodegenResults FieldAccessNode::codegen(CodegenContext &cc) {
  CodegenResults BASE = base->codegen(cc);
  std::vector<std::pair<std::string, size_t>> PairList;

  if (auto ST = llvm::dyn_cast<llvm::StructType>(BASE.ActualType)) {
    auto it = cc.StructsToPair.find(ST);

    if (it != cc.StructsToPair.end()) {
      PairList = it->second;

    } else {
      throw std::runtime_error("Unable To Fine Value");
    }
  }

  size_t index = -1;
  for (auto x : PairList) {
    if (x.first == name) {
      index = x.second;
      break;
    }
  }
  auto gep = cc.Builder->CreateStructGEP(BASE.ActualType,
                                         BASE.ActualValueButAsAPointer, index);

  return {gep, gep, BASE.ActualType, BASE.ActualTypeButNotThePointer};
}

// int main() {
//   CodegenContext ctx("myprogram");
//   ctx.pushScope(); // Start Global Scope

//   // --- First compound for "random" function ---
//   std::vector<std::unique_ptr<ast>> vals;

//   vals.push_back(std::make_unique<VariableDeclareNode>(
//       "val2", std::make_unique<VariableReferenceNode>("val1"),
//       Token{TokenType::TYPES, "INTEGER"}));

//   vals.push_back(std::make_unique<WhileNode>(
//       std::make_unique<VariableReferenceNode>("val2"),
//       std::make_unique<ContinueNode>()));

//   vals.push_back(std::make_unique<IfNode>(
//       std::make_unique<VariableReferenceNode>("val2"),
//       std::make_unique<IntegerNode>(21),
//       std::make_unique<IntegerNode>(32)));

//   vals.push_back(
//       std::make_unique<ReturnNode>(std::make_unique<BinaryOperationNode>(
//           TokenType::GTE,
//           std::make_unique<VariableReferenceNode>("val1"),
//           std::make_unique<VariableReferenceNode>("val2"))));

//   auto compoundRandom = std::make_unique<CompoundNode>(std::move(vals));

//   std::vector<std::pair<std::string, llvm::Type *>> typeRandom = {
//       {"val1", llvm::Type::getInt32Ty(*ctx.TheContext)}};

//   auto RandomFunction = std::make_unique<FunctionNode>(
//       "random", typeRandom, std::move(compoundRandom),
//       Token{TokenType::TYPES, "INTEGER"});

//   // --- Second compound for "main" function ---
//   std::vector<std::unique_ptr<ast>> anothervals;

//   anothervals.push_back(std::make_unique<VariableDeclareNode>(
//       "val1", std::make_unique<IntegerNode>(21),
//       Token{TokenType::TYPES, "INTEGER"}));

//   // Prepare arguments vector separately to move unique_ptrs
//   std::vector<std::unique_ptr<ast>> callArgs;
//   callArgs.push_back(std::make_unique<VariableReferenceNode>("val1"));

//   anothervals.push_back(
//       std::make_unique<CallNode>("random", std::move(callArgs)));

//   std::vector<std::unique_ptr<ast>> elements;
//   elements.push_back(std::make_unique<CharNode>('a'));
//   elements.push_back(std::make_unique<CharNode>('b'));
//   elements.push_back(std::make_unique<CharNode>('c'));

//   anothervals.push_back(std::make_unique<ArrayLiteralNode>(
//       llvm::Type::getInt32Ty(*ctx.TheContext), std::move(elements)));

//   auto anotherCompound =
//   std::make_unique<CompoundNode>(std::move(anothervals));

//   auto Function = std::make_unique<FunctionNode>(
//       "main", typeRandom, std::move(anotherCompound),
//       Token{TokenType::TYPES, "INTEGER"});

//   // --- Codegen ---

//   RandomFunction->codegen(ctx);
//   Function->codegen(ctx);

//   ctx.Module->print(llvm::errs(), nullptr);

//   ctx.popScope(); // End Global Scope
//   return 0;
// }
