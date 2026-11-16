#include "lexer.h"
#include "utils.h"
#include <alloca.h>
#include <ast.h>
#include <colors.h>
#include <cstddef>
#include <iostream>
#include <llvm-21/llvm/TargetParser/Triple.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constant.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/DiagnosticHandler.h>
#include <llvm/IR/FMF.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalValue.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/TypeName.h>
#include <llvm/Support/raw_ostream.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <strings.h>
#include <types.h>
#include <vector>

llvm::Value *castValue(llvm::IRBuilder<> &builder, llvm::Value *val,
                       llvm::Type *targetType, bool sourceIsSigned,
                       bool targetIsSigned) {
  std::cout << "CATING HERE: " << std::endl;
  if (!val || !targetType)
    return nullptr;

  llvm::Type *srcType = val->getType();

  if (srcType == targetType)
    return val;

  if (srcType->isIntegerTy() && targetType->isIntegerTy())
    return builder.CreateIntCast(val, targetType, sourceIsSigned);

  if (srcType->isIntegerTy() && targetType->isFloatingPointTy())
    return sourceIsSigned ? builder.CreateSIToFP(val, targetType)
                          : builder.CreateUIToFP(val, targetType);

  if (srcType->isFloatingPointTy() && targetType->isIntegerTy())
    return targetIsSigned ? builder.CreateFPToSI(val, targetType)
                          : builder.CreateFPToUI(val, targetType);

  if (srcType->isFloatingPointTy() && targetType->isFloatingPointTy())
    return builder.CreateFPCast(val, targetType);

  if (srcType->isPointerTy() && targetType->isPointerTy()) {
    if (srcType->getPointerAddressSpace() ==
        targetType->getPointerAddressSpace())
      return builder.CreateBitCast(val, targetType);

    return builder.CreateAddrSpaceCast(val, targetType);
  }

  if (srcType->isPointerTy() && targetType->isIntegerTy())
    return builder.CreatePtrToInt(val, targetType);

  if (srcType->isIntegerTy() && targetType->isPointerTy())
    return builder.CreateIntToPtr(val, targetType);

  return nullptr;
}

CodegenResults CharNode::codegen(CodegenContext &cc) {
  return {
      llvm::ConstantInt::get(llvm::Type::getInt8Ty(*cc.TheContext), val, false),
      nullptr, CreateSystemType(SINGED, 8)};
}

CodegenResults StringNode::codegen(CodegenContext &cc) {
  auto stringConstant = llvm::ConstantDataArray::getString(*cc.TheContext, val);

  auto *strGlobal = new llvm::GlobalVariable(
      *cc.Module, stringConstant->getType(), true,
      llvm::GlobalValue::PrivateLinkage, stringConstant, ".str");

  // i8* pointer to first character
  llvm::Value *zero =
      llvm::ConstantInt::get(llvm::Type::getInt64Ty(*cc.TheContext), 0);

  llvm::Value *strPtr = cc.Builder->CreateInBoundsGEP(stringConstant->getType(),
                                                      strGlobal, {zero, zero});
  return {strPtr, strGlobal,
          CreateSystemType(SINGED, 8, true, 1, true,
                           stringConstant->getType()->getArrayNumElements())};
}

CodegenResults IntegerNode::codegen(CodegenContext &cc) {
  auto type = CreateSystemType(is_singed ? SINGED : UNSINGED, size);

  return {llvm::ConstantInt::get(
              llvm::dyn_cast<llvm::IntegerType>(ComputeType(*type, cc)),
              std::stoll(val), is_singed),
          nullptr, std::move(type)};
}

CodegenResults FloatNode::codegen(CodegenContext &cc) {

  auto type = CreateSystemType(FLOAT, size);

  return {llvm::ConstantFP::get(ComputeType(*type, cc), std::stold(val)),
          nullptr, std::move(type)};
}

CodegenResults BooleanNode::codegen(CodegenContext &cc) {
  return {
      llvm::ConstantInt::get(llvm::Type::getInt1Ty(*cc.TheContext), val, true),
      nullptr, CreateSystemType(UNSINGED, 1)};
}

CodegenResults VariableDeclareNode::codegen(CodegenContext &cc) {
  llvm::Type *elementType = ComputeType(*type, cc);
  CodegenResults result = val->codegen(cc);

  llvm::AllocaInst *alloca =
      cc.Builder->CreateAlloca(elementType, nullptr, name);

  llvm::Value *castedValue = castValue(
      *cc.Builder, result.ActualValue, elementType,
      result.Type->kind ? SINGED : UNSINGED, type->kind ? SINGED : UNSINGED);

  cc.Builder->CreateStore(castedValue, alloca);

  cc.addVariable(name, alloca, *type);
  return {cc.Builder->CreateLoad(elementType, alloca), alloca, std::move(type)};
}

CodegenResults AssignmentNode::codegen(CodegenContext &cc) {

  CodegenResults LHS = lhs->codegen(cc);
  CodegenResults RHS = rhs->codegen(cc);

  if (!LHS.ActualValueButAsAPointer || !RHS.ActualValue) {
    std::cerr << "The Problem sis at AssignmentNode" << std::endl;
  }

  return {
      cc.Builder->CreateStore(RHS.ActualValue, LHS.ActualValueButAsAPointer),
      nullptr, std::move(LHS.Type)};
}

CodegenResults ReturnNode::codegen(CodegenContext &cc) {
  if (expr) {
    CodegenResults retVal = expr->codegen(cc);
    return {cc.Builder->CreateRet(retVal.ActualValue),
            retVal.ActualValueButAsAPointer, std::move(retVal.Type)};
  } else {
    return {cc.Builder->CreateRetVoid(), nullptr, nullptr};
  }
}

CodegenResults CompoundNode::codegen(CodegenContext &cc) {
  CodegenResults last = {nullptr, nullptr, nullptr};

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
    argTypes.push_back(ComputeType(*std::get<1>(a), cc));

  llvm::Type *retTy = ComputeType(*ReturnType, cc);
  auto *FT = llvm::FunctionType::get(retTy, argTypes, isVaridic);
  auto *Fn = llvm::Function::Create(FT, llvm::Function::ExternalLinkage, name,
                                    cc.Module.get());

  auto *BB = llvm::BasicBlock::Create(*cc.TheContext, "entry", Fn);
  cc.Builder->SetInsertPoint(BB);
  cc.pushScope();

  unsigned i = 0;
  for (auto &arg : Fn->args()) {
    // Access current index safely before incrementing
    const auto &argName = std::get<0>(args[i]);
    SystemType &argTypeSpec = *std::get<1>(args[i]);

    arg.setName(argName);
    llvm::Type *argType = arg.getType();
    auto *alloca = cc.Builder->CreateAlloca(argType, nullptr, argName);
    cc.Builder->CreateStore(&arg, alloca);

    if (argTypeSpec.is_ptr || argTypeSpec.ptrdepth > 0) {
      SystemType underlyingType = argTypeSpec;

      underlyingType.is_ptr = false;

      if (underlyingType.ptrdepth > 0) {
        underlyingType.ptrdepth--;
      }
    }

    cc.addVariable(argName, alloca, argTypeSpec);

    i++;
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
        return {nullptr, nullptr, nullptr};
      }
      cc.Builder->CreateRet(retVal.ActualValue);
    }
  }

  llvm::verifyFunction(*Fn);
  cc.popScope();
  return {Fn, nullptr, std::move(ReturnType)};
}

CodegenResults VariableReferenceNode::codegen(CodegenContext &cc) {
  auto [ptr, type] = cc.lookupVariable(Name); // pointer

  if (!ptr) {
    throw std::runtime_error(
        "VariableReferenceNode Cannot find Variable named: " + Name);
  }

  // if (!ptr.first) {
  //   auto it = cc.StringToStructs.find(Name);
  //   if (it != cc.StringToStructs.end()) {
  //     ptr.second = it->second;
  //   }
  // }

  return {cc.Builder->CreateLoad(ComputeType(type, cc), ptr, Name), ptr,
          std::make_unique<SystemType>(type)};
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
    return {nullptr, nullptr, nullptr};

  llvm::Value *condVal = cond.ActualValue;

  // Convert condition to i1 if necessary
  if (!condVal->getType()->isIntegerTy(1)) {
    condVal = cc.Builder->CreateICmpNE(
        condVal, llvm::ConstantInt::get(condVal->getType(), 0),
        "while.cond.to.i1");
  }

  cc.Builder->CreateCondBr(condVal, bodyBB, afterBB);

  llvm::BasicBlock *oldBreak = cc.BreakBB;
  llvm::BasicBlock *oldCont = cc.ContinueBB;

  cc.BreakBB = afterBB;
  cc.ContinueBB = condBB;

  cc.Builder->SetInsertPoint(bodyBB);

  if (body) {
    body->codegen(cc);
  }

  if (!cc.Builder->GetInsertBlock()->getTerminator()) {
    cc.Builder->CreateBr(condBB);
  }

  cc.BreakBB = oldBreak;
  cc.ContinueBB = oldCont;

  cc.Builder->SetInsertPoint(afterBB);

  return {nullptr, nullptr, nullptr};
}

CodegenResults IfNode::codegen(CodegenContext &cc) {
  CodegenResults condR = condition->codegen(cc);

  if (!condR.ActualValue)
    return {nullptr, nullptr, nullptr};

  llvm::Value *condV = condR.ActualValue;

  if (!condV->getType()->isIntegerTy(1)) {
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

  cc.Builder->SetInsertPoint(thenBB);

  cc.pushScope();

  if (thenBlock)
    thenBlock->codegen(cc);

  cc.popScope();

  if (!cc.Builder->GetInsertBlock()->getTerminator())
    cc.Builder->CreateBr(mergeBB);

  if (elseBB) {
    cc.Builder->SetInsertPoint(elseBB);

    cc.pushScope();

    if (elseBlock)
      elseBlock->codegen(cc);

    cc.popScope();

    if (!cc.Builder->GetInsertBlock()->getTerminator())
      cc.Builder->CreateBr(mergeBB);
  }

  cc.Builder->SetInsertPoint(mergeBB);

  return {nullptr, nullptr, nullptr};
}

CodegenResults BinaryOperationNode::codegen(CodegenContext &cc) {
  CodegenResults L = Left->codegen(cc);
  CodegenResults R = Right->codegen(cc);

  if (!L.ActualValue || !R.ActualValue)
    throw std::runtime_error("Null operand in binary operation");

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
      if (LT->isIntegerTy() && RT->isIntegerTy())
        RHS = cc.Builder->CreateIntCast(RHS, LT, true);
      else
        throw std::runtime_error(
            "Cannot perform arithmetic on incompatible types");
    }

    llvm::Value *result = nullptr;

    switch (Type) {
    case TokenType::PLUS:
      result = cc.Builder->CreateAdd(LHS, RHS, "addtmp");
      break;
    case TokenType::MINUS:
      result = cc.Builder->CreateSub(LHS, RHS, "subtmp");
      break;
    case TokenType::STAR:
      result = cc.Builder->CreateMul(LHS, RHS, "multmp");
      break;
    case TokenType::SLASH:
      result = cc.Builder->CreateSDiv(LHS, RHS, "divtmp");
      break;
    default:
      break;
    }

    return {result, nullptr, std::make_unique<SystemType>(*L.Type)};
  }

  case TokenType::EQEQ:
  case TokenType::NOTEQ:
  case TokenType::GTE:
  case TokenType::LTE:
  case TokenType::GT:
  case TokenType::LT: {
    if (LT != RT) {
      if (LT->isIntegerTy() && RT->isIntegerTy())
        RHS = cc.Builder->CreateIntCast(RHS, LT, true);
      else
        throw std::runtime_error("Cannot compare incompatible types");
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

    auto resultType = std::make_unique<SystemType>();
    resultType->kind = UNSINGED;
    resultType->size = 1;

    return {result, nullptr, std::move(resultType)};
  }

  case TokenType::AND: {
    if (!LT->isIntegerTy(1))
      LHS = cc.Builder->CreateICmpNE(LHS, llvm::ConstantInt::get(LT, 0),
                                     "lhsbool");

    if (!RT->isIntegerTy(1))
      RHS = cc.Builder->CreateICmpNE(RHS, llvm::ConstantInt::get(RT, 0),
                                     "rhsbool");

    llvm::Value *result = cc.Builder->CreateAnd(LHS, RHS, "andtmp");

    auto resultType = std::make_unique<SystemType>();
    resultType->kind = UNSINGED;
    resultType->size = 1;

    return {result, nullptr, std::move(resultType)};
  }

  default:
    throw std::runtime_error("Unknown binary operator");
  }
}

CodegenResults BreakNode::codegen(CodegenContext &cc) {
  if (!cc.BreakBB) {
    std::cerr << "Error: 'break' not inside a loop.\n";
    return {nullptr, nullptr, nullptr};
  }
  return {cc.Builder->CreateBr(cc.BreakBB), nullptr, nullptr};
}

CodegenResults CallNode::codegen(CodegenContext &cc) {
  llvm::Function *callee = cc.Module->getFunction(name);

  if (!callee)
    return {nullptr, nullptr, nullptr};

  if (callee->arg_size() != args.size())
    throw std::runtime_error("Argument count mismatch in function call");

  std::vector<llvm::Value *> argVals;

  auto it = callee->arg_begin();

  for (auto &arg : args) {
    CodegenResults r = arg->codegen(cc);

    if (!r.ActualValue)
      return {nullptr, nullptr, nullptr};

    llvm::Value *v = r.ActualValue;
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

  llvm::CallInst *call = cc.Builder->CreateCall(callee, argVals);

  auto returnType = std::make_unique<SystemType>();

  if (callee->getReturnType()->isVoidTy()) {
    returnType->kind = VOID;
  } else if (callee->getReturnType()->isIntegerTy()) {
    returnType->kind = SINGED;
    returnType->size = callee->getReturnType()->getIntegerBitWidth() / 8;
  } else if (callee->getReturnType()->isFloatingPointTy()) {
    returnType->kind = FLOAT;
    returnType->size = callee->getReturnType()->getPrimitiveSizeInBits() / 8;
  }

  return {callee->getReturnType()->isVoidTy() ? nullptr : call, nullptr,
          std::move(returnType)};
}

CodegenResults ContinueNode::codegen(CodegenContext &cc) {
  if (!cc.ContinueBB) {
    throw std::runtime_error("'continue' used outside a loop");
  }

  cc.Builder->CreateBr(cc.ContinueBB);

  return {nullptr, nullptr, nullptr};
}

CodegenResults ForNode::codegen(CodegenContext &cc) {
  llvm::Function *function = cc.Builder->GetInsertBlock()->getParent();

  if (init)
    init->codegen(cc);

  llvm::BasicBlock *loopCondBB =
      llvm::BasicBlock::Create(*cc.TheContext, "for.cond", function);

  llvm::BasicBlock *loopBodyBB =
      llvm::BasicBlock::Create(*cc.TheContext, "for.body", function);

  llvm::BasicBlock *loopIncBB =
      llvm::BasicBlock::Create(*cc.TheContext, "for.inc", function);

  llvm::BasicBlock *loopEndBB =
      llvm::BasicBlock::Create(*cc.TheContext, "for.end", function);

  llvm::BasicBlock *oldBreak = cc.BreakBB;
  llvm::BasicBlock *oldCont = cc.ContinueBB;

  cc.BreakBB = loopEndBB;
  cc.ContinueBB = loopIncBB;

  cc.Builder->CreateBr(loopCondBB);

  // CONDITION
  cc.Builder->SetInsertPoint(loopCondBB);

  if (condition) {
    CodegenResults condR = condition->codegen(cc);

    if (!condR.ActualValue) {
      cc.BreakBB = oldBreak;
      cc.ContinueBB = oldCont;
      return {nullptr, nullptr, nullptr};
    }

    llvm::Value *condValue = condR.ActualValue;

    if (!condValue->getType()->isIntegerTy(1)) {
      condValue = cc.Builder->CreateICmpNE(
          condValue, llvm::ConstantInt::get(condValue->getType(), 0),
          "forcond");
    }

    cc.Builder->CreateCondBr(condValue, loopBodyBB, loopEndBB);
  } else {
    cc.Builder->CreateBr(loopBodyBB);
  }

  // BODY
  cc.Builder->SetInsertPoint(loopBodyBB);

  if (body)
    body->codegen(cc);

  if (!cc.Builder->GetInsertBlock()->getTerminator())
    cc.Builder->CreateBr(loopIncBB);

  // INCREMENT
  cc.Builder->SetInsertPoint(loopIncBB);

  if (increment)
    increment->codegen(cc);

  if (!cc.Builder->GetInsertBlock()->getTerminator())
    cc.Builder->CreateBr(loopCondBB);

  // RESTORE LOOP CONTEXT
  cc.BreakBB = oldBreak;
  cc.ContinueBB = oldCont;

  // END
  cc.Builder->SetInsertPoint(loopEndBB);

  return {nullptr, nullptr, nullptr};
}

CodegenResults ArrayLiteralNode::codegen(CodegenContext &cc) {
  if (Elements.empty()) {
    auto *elementType = llvm::Type::getInt8Ty(*cc.TheContext);
    auto *arrayType = llvm::ArrayType::get(elementType, 0);

    auto resultType = std::make_unique<SystemType>();
    resultType->is_arr = true;
    resultType->size_arr = 0;
    resultType->kind = UNSINGED;
    resultType->size = 1;

    return {llvm::ConstantArray::get(arrayType, {}), nullptr,
            std::move(resultType)};
  }

  std::vector<llvm::Constant *> constantValues;

  for (auto &element : Elements) {
    CodegenResults result = element->codegen(cc);

    if (!result.ActualValue)
      throw std::runtime_error("Invalid array element");

    auto *constant = llvm::dyn_cast<llvm::Constant>(result.ActualValue);

    if (!constant)
      throw std::runtime_error(
          "Array literal elements must be compile-time constants");

    constantValues.push_back(constant);
  }

  llvm::Type *elementType = constantValues[0]->getType();

  for (auto *value : constantValues) {
    if (value->getType() != elementType)
      throw std::runtime_error(
          "Array literal elements must have matching types");
  }

  llvm::ArrayType *arrayType =
      llvm::ArrayType::get(elementType, constantValues.size());

  auto resultType = std::make_unique<SystemType>();
  resultType->is_arr = true;
  resultType->size_arr = constantValues.size();

  // Populate the element type metadata here.
  // SystemType currently has no separate element-type field.

  return {llvm::ConstantArray::get(arrayType, constantValues), nullptr,
          std::move(resultType)};
}

CodegenResults ArrayAccessNode::codegen(CodegenContext &cc) {
  auto [arrayPtr, arrayType] = cc.lookupVariable(arrayName);

  if (!arrayPtr)
    throw std::runtime_error("Array not found: " + arrayName);

  CodegenResults idx = indexExpr->codegen(cc);

  if (!idx.ActualValue)
    throw std::runtime_error("Invalid array index");

  if (!idx.ActualValue->getType()->isIntegerTy())
    throw std::runtime_error("Array index must be an integer");

  llvm::Value *elementPtr = nullptr;

  SystemType elementSystemType = arrayType;
  elementSystemType.is_ptr = false;
  elementSystemType.ptrdepth = 0;
  elementSystemType.is_arr = false;
  elementSystemType.size_arr = 0;

  llvm::Type *elementType = ComputeType(elementSystemType, cc);
  llvm::Type *arrayLLVMType = ComputeType(arrayType, cc);

  if (arrayType.is_arr) {
    llvm::Value *indices[] = {cc.Builder->getInt32(0), idx.ActualValue};

    elementPtr = cc.Builder->CreateInBoundsGEP(arrayLLVMType, arrayPtr, indices,
                                               "arrayidx");

  } else if (arrayType.is_ptr) {
    llvm::Value *realPtr =
        cc.Builder->CreateLoad(arrayLLVMType, arrayPtr, "arrayptr");

    elementPtr = cc.Builder->CreateInBoundsGEP(elementType, realPtr,
                                               idx.ActualValue, "arrayidx");

  } else {
    throw std::runtime_error("Variable is not an array: " + arrayName);
  }

  return {cc.Builder->CreateLoad(elementType, elementPtr, "arrayelem"),
          elementPtr, std::make_unique<SystemType>(elementSystemType)};
}

CodegenResults PointerReferenceNode::codegen(CodegenContext &cc) {
  CodegenResults var = name->codegen(cc);

  if (!var.ActualValueButAsAPointer)
    throw std::runtime_error("Cannot take address of this value");

  SystemType pointerType = *var.Type;
  pointerType.is_ptr = true;
  pointerType.ptrdepth++;

  return {var.ActualValueButAsAPointer, nullptr,
          std::make_unique<SystemType>(std::move(pointerType))};
}

CodegenResults DeReferenceNode::codegen(CodegenContext &cc) {
  auto [var, type] = cc.lookupVariable(name);

  if (!var)
    throw std::runtime_error("Unknown variable: " + name);

  if (!type.is_ptr || type.ptrdepth == 0)
    throw std::runtime_error("'" + name + "' is not a pointer");

  SystemType elementSystemType = type;
  elementSystemType.is_ptr = false;
  elementSystemType.ptrdepth = 0;

  if (type.ptrdepth > 1) {
    elementSystemType.is_ptr = true;
    elementSystemType.ptrdepth = type.ptrdepth - 1;
  }

  llvm::Type *pointerType = ComputeType(type, cc);
  llvm::Type *elementType = ComputeType(elementSystemType, cc);

  llvm::Value *ptrVal = cc.Builder->CreateLoad(pointerType, var, name + "_ptr");

  if (index) {
    CodegenResults idx = index->codegen(cc);

    if (!idx.ActualValue)
      throw std::runtime_error("Invalid pointer index");

    ptrVal =
        cc.Builder->CreateGEP(elementType, ptrVal, idx.ActualValue, "ptr_elem");
  }

  return {cc.Builder->CreateLoad(elementType, ptrVal, "deref_" + name), ptrVal,
          std::make_unique<SystemType>(std::move(elementSystemType))};
}

CodegenResults CastNode::codegen(CodegenContext &cc) {
  if (!Value || !targetType)
    throw std::runtime_error("Invalid cast expression");

  CodegenResults v = Value->codegen(cc);

  if (!v.ActualValue || !v.Type)
    throw std::runtime_error("Cannot cast this value");

  llvm::Type *targetLLVMType = ComputeType(*targetType, cc);

  if (!targetLLVMType)
    throw std::runtime_error("Invalid target type for cast");

  bool sourceIsSigned = v.Type->kind == SINGED;
  bool targetIsSigned = targetType->kind == SINGED;

  llvm::Value *result = castValue(*cc.Builder, v.ActualValue, targetLLVMType,
                                  sourceIsSigned, targetIsSigned);

  if (!result)
    throw std::runtime_error("Unsupported cast");

  return {result, nullptr, std::make_unique<SystemType>(*targetType)};
}

CodegenResults StructCreateNode::codegen(CodegenContext &cc) {
  llvm::StructType *theStruct = llvm::StructType::create(*cc.TheContext, name);

  // Register first so pointer fields can refer to this struct.
  cc.addStruct(name, theStruct, {});

  std::vector<llvm::Type *> fieldTypes;
  std::vector<std::tuple<std::string, size_t, SystemType>> fields;

  fieldTypes.reserve(types.size());
  fields.reserve(types.size());

  size_t index = 0;
  for (const auto &[fieldName, fieldType] : types) {
    if (!fieldType)
      throw std::runtime_error("Missing type for field: " + fieldName);

    SystemType systemType = *fieldType;
    llvm::Type *llvmType = ComputeType(systemType, cc);

    if (!llvmType || !llvm::StructType::isValidElementType(llvmType))
      throw std::runtime_error("Invalid type for field: " + fieldName);

    fieldTypes.push_back(llvmType);
    fields.emplace_back(fieldName, index++, systemType);
  }

  theStruct->setBody(fieldTypes);
  cc.addStruct(name, theStruct, std::move(fields));

  return {nullptr, nullptr, nullptr};
}

CodegenResults FieldAccessNode::codegen(CodegenContext &cc) {
  if (!base)
    throw std::runtime_error("FieldAccess: missing base expression");

  CodegenResults baseResult = base->codegen(cc);

  if (!baseResult.Type)
    throw std::runtime_error("FieldAccess: base type is null");

  const SystemType &baseType = *baseResult.Type;

  if (baseType.kind != STRUCTS || baseType.is_arr)
    throw std::runtime_error("Field access on non-struct type");

  if (baseType.is_ptr && baseType.ptrdepth != 1)
    throw std::runtime_error(
        "Field access requires a struct or a single-level struct pointer");

  llvm::StructType *structType = cc.lookupStruct(baseType.struct_name);

  auto it = cc.StructsToPair.find(structType);
  if (it == cc.StructsToPair.end())
    throw std::runtime_error("Unknown struct type: " + baseType.struct_name);

  for (const auto &[fieldName, index, fieldType] : it->second) {
    if (fieldName != name)
      continue;

    auto resultType = std::make_unique<SystemType>(fieldType);
    llvm::Type *llvmFieldType = ComputeType(*resultType, cc);

    if (!llvmFieldType)
      throw std::runtime_error("Invalid type for field: " + name);

    // For a struct pointer, its value is the address of the struct.
    // For a struct lvalue, use its stored address.
    llvm::Value *structAddress = baseType.is_ptr
                                     ? baseResult.ActualValue
                                     : baseResult.ActualValueButAsAPointer;

    if (structAddress) {
      if (!structAddress->getType()->isPointerTy())
        throw std::runtime_error("FieldAccess: base address is not a pointer");

      llvm::Value *fieldAddress =
          cc.Builder->CreateStructGEP(structType, structAddress, index);

      llvm::Value *fieldValue =
          cc.Builder->CreateLoad(llvmFieldType, fieldAddress);

      return {fieldValue, fieldAddress, std::move(resultType)};
    }

    // A struct rvalue can be read, but its field has no assignable address.
    if (!baseType.is_ptr && baseResult.ActualValue) {
      llvm::Value *fieldValue = cc.Builder->CreateExtractValue(
          baseResult.ActualValue, {static_cast<unsigned>(index)});

      return {fieldValue, nullptr, std::move(resultType)};
    }

    throw std::runtime_error("FieldAccess: base has no usable value");
  }

  throw std::runtime_error("Invalid field: " + name);
}

CodegenResults PointerFieldAccessNode::codegen(CodegenContext &cc) {
  if (!base)
    throw std::runtime_error("PointerFieldAccess: missing base expression");

  CodegenResults BASE = base->codegen(cc);

  if (!BASE.Type)
    throw std::runtime_error("PointerFieldAccess: base type is null");

  if (BASE.Type->kind != STRUCTS || !BASE.Type->is_ptr ||
      BASE.Type->ptrdepth != 1 || BASE.Type->is_arr) {
    throw std::runtime_error(
        "PointerFieldAccess: base must be a single-level struct pointer");
  }

  llvm::Value *basePtr = BASE.ActualValue;

  if (!basePtr || !basePtr->getType()->isPointerTy())
    throw std::runtime_error("PointerFieldAccess: base is not a pointer value");

  llvm::StructType *structType = cc.lookupStruct(BASE.Type->struct_name);

  auto it = cc.StructsToPair.find(structType);
  if (it == cc.StructsToPair.end())
    throw std::runtime_error("PointerFieldAccess: unknown struct type: " +
                             BASE.Type->struct_name);

  for (const auto &[fieldName, index, fieldType] : it->second) {
    if (fieldName != name)
      continue;

    auto resultType = std::make_unique<SystemType>(fieldType);
    llvm::Type *llvmFieldType = ComputeType(*resultType, cc);

    if (!llvmFieldType)
      throw std::runtime_error("PointerFieldAccess: invalid type for field: " +
                               name);

    llvm::Value *fieldPtr =
        cc.Builder->CreateStructGEP(structType, basePtr, index);

    llvm::Value *fieldValue = cc.Builder->CreateLoad(llvmFieldType, fieldPtr);

    return {fieldValue, fieldPtr, std::move(resultType)};
  }

  throw std::runtime_error("Invalid field: " + name);
}
