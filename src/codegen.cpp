#include "lexer.h"
#include <alloca.h>
#include <ast.h>
#include <cctype>
#include <colors.h>
#include <cstddef>
#include <iostream>
#include <llvm/IR/Module.h>
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

// ABUNDANT
// llvm::Type *GetPointeeType(Token typeToken, CodegenContext &cc) {
//   std::string t = typeToken.value;
//   for (char &c : t)
//     c = toupper(c);

//   if (t.size() > 7 && t.substr(t.size() - 7) == "POINTER") {
//     Token baseToken;
//     baseToken.value = t.substr(0, t.size() - 7); // strip "POINTER"
//     return GetTypeNonVoid(baseToken, cc); // "CHAR" -> i8, "INTEGER" -> i32
//   }
//   return nullptr;
// }

// llvm::Type *GetTypeNonVoid(Token type, CodegenContext &cc) {
//   llvm::Type *retTy;

//   if (type.type == IDENTIFIER) {
//     retTy = cc.lookupStruct(type.value);

//   } else if (type.type == TYPES) {
//     std::string t = type.value;

//     for (auto &i : t) {
//       i = toupper(i);
//     }

//     if (t == "INTEGER") {
//       retTy = llvm::Type::getInt32Ty(*cc.TheContext);
//     } else if (t == "FLOAT") {
//       retTy = llvm::Type::getFloatTy(*cc.TheContext);
//     } else if (t == "STRING") {
//       retTy = llvm::Type::getInt8Ty(*cc.TheContext);
//     } else if (t == "BOOLEAN") {
//       retTy = llvm::Type::getInt1Ty(*cc.TheContext);
//     } else if (t == "CHAR") {
//       retTy = llvm::Type::getInt8Ty(*cc.TheContext);
//     } else if (t == "VOID" && type.ptrdepth > 0) {
//       retTy = llvm::PointerType::get(*cc.TheContext, 0);
//     }

//   } else {
//     throw std::runtime_error("INVALID TYPE: " + type.value);
//   }

//   // for (int i = type.ptrdepth; i > 0; --i) {
//   //   retTy = llvm::PointerType::get(retTy, 0);
//   // }
//   if (type.ptrdepth > 0) {
//     retTy = llvm::PointerType::get(retTy, 0);
//   }

//   return retTy;
// }

// llvm::Type *GetTypeVoid(Token type, CodegenContext &cc) {
//   std::string holder = type.value;
//   for (char &c : holder)
//     c = toupper(c);

//   if (holder == "VOID") {
//     if (type.ptrdepth > 0)
//       return llvm::PointerType::get(*cc.TheContext, 0);
//     return llvm::Type::getVoidTy(*cc.TheContext);
//   }

//   return GetTypeNonVoid(type, cc);
// }

CodegenResults CharNode::codegen(CodegenContext &cc) {
  return {
      llvm::ConstantInt::get(llvm::Type::getInt8Ty(*cc.TheContext), val, false),
      nullptr, llvm::Type::getInt8Ty(*cc.TheContext), nullptr};
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
  return {strPtr, strGlobal, strPtr->getType(), strGlobal->getType()};
}

CodegenResults IntegerNode::codegen(CodegenContext &cc) {
  // std::cout << "CALLING ME" << std::endl;
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
  llvm::Type *elementType = ComputeType(type, cc);
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

  llvm::Type *elemType = ComputeType(type, cc);

  if (arraySize.has_value()) {
    finalType = llvm::ArrayType::get(elemType, arraySize.value());
  } else {
    finalType = elemType;
  }

  auto holder = type;
  holder.is_ptr = false;
  llvm::Type *notptr = ComputeType(holder, cc);

  cc.addVariable(name, alloca, finalType, elemType);
  return {
      cc.Builder->CreateLoad(finalType, alloca), // ActualValue (the data)
      alloca,    // ActualValueButAsAPointer (the address)
      finalType, // ActualType (pointer type)
      notptr     // ActualTypeButNotThePointer
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
    argTypes.push_back(ComputeType(std::get<1>(a), cc));

  llvm::Type *retTy = ComputeType(ReturnType, cc);
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
    SystemType &argTypeSpec = std::get<1>(args[i]);

    arg.setName(argName);
    llvm::Type *argType = arg.getType();
    auto *alloca = cc.Builder->CreateAlloca(argType, nullptr, argName);
    cc.Builder->CreateStore(&arg, alloca);

    llvm::Type *pointeeType = nullptr;

    if (argTypeSpec.is_ptr || argTypeSpec.ptrdepth > 0) {
      SystemType underlyingType = argTypeSpec;

      underlyingType.is_ptr = false;

      if (underlyingType.ptrdepth > 0) {
        underlyingType.ptrdepth--;
      }

      // underlyingType.theLLvmtType = nullptr;

      pointeeType = ComputeType(underlyingType, cc);
    }

    cc.addVariable(argName, alloca, argType, pointeeType);

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

  if (!ptr.val) {
    throw std::runtime_error(
        "VariableReferenceNode Cannot find Variable named: " + Name);
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

  case TokenType::BITOR: {

    if (!LT->isIntegerTy() || !RT->isIntegerTy()) {
      throw std::runtime_error("Bitwise OR requires integer operands");
    }

    auto *i32 = llvm::Type::getInt32Ty(*cc.TheContext);

    if (LT->isIntegerTy(1))
      LHS = cc.Builder->CreateIntCast(LHS, i32, false);
    if (RT->isIntegerTy(1))
      RHS = cc.Builder->CreateIntCast(RHS, i32, false);

    LT = LHS->getType();
    RT = RHS->getType();

    if (LT != RT) {
      if (LT->isIntegerTy() && RT->isIntegerTy()) {
        RHS = cc.Builder->CreateIntCast(RHS, LT, false);
      } else {
        throw std::runtime_error("Cannot bitwise OR incompatible types");
      }
    }

    llvm::Value *result = cc.Builder->CreateOr(LHS, RHS, "ortmp");

    return {result, nullptr, LT, LT};
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
  std::cout << Colors::RED << "WORK INSIDE SYSCALL" << Colors::RESET << std::endl;
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
  CodegenResults var = name->codegen(cc);
  if (!var.ActualValue) {
    throw std::runtime_error("CANNOT FIND VALUE ");
  }
  return {var.ActualValueButAsAPointer, var.ActualValueButAsAPointer,
          var.ActualType, var.ActualTypeButNotThePointer};
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
  return {
      castValue(*cc.Builder, v.ActualValue, ComputeType(targetType, cc), true),
      nullptr, v.ActualType, v.ActualTypeButNotThePointer};
}

CodegenResults StructCreateNode::codegen(CodegenContext &cc) {
  llvm::StructType *TheStruct = llvm::StructType::create(*cc.TheContext, name);
  cc.addStruct(name, TheStruct,
               std::vector<std::tuple<std::string, size_t, llvm::Type *>>());
  std::vector<llvm::Type *> fieldTypes;
  fieldTypes.reserve(types.size());

  std::vector<std::tuple<std::string, size_t, llvm::Type *>> indexs;
  size_t i = 0;
  for (const auto &p : types) {
    SystemType mutableType = p.second;
    auto type = ComputeType(mutableType, cc);
    fieldTypes.push_back(type);
    indexs.push_back({p.first, i, type});
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

  llvm::Type *T = BASE.ActualType;
  if (T->isPointerTy()) {
    T = BASE.ActualTypeButNotThePointer;
  }

  if (!T) {
    throw std::runtime_error("FieldAccess: base type is null");
  }

  auto ST = llvm::dyn_cast<llvm::StructType>(T);
  if (!ST) {
    throw std::runtime_error("Field access on non-struct type");
  }

  auto it = cc.StructsToPair.find(ST);
  if (it == cc.StructsToPair.end()) {
    throw std::runtime_error("Unknown struct type");
  }

  const auto &PairList = it->second;

  size_t index = (size_t)-1;

  llvm::Type *FieldType;
  for (auto &x : PairList) {
    if (std::get<0>(x) == name) {
      index = std::get<1>(x);
      FieldType = std::get<2>(x);
      break;
    }
  }

  if (index == (size_t)-1) {
    std::cerr << "Available Fields" << std::endl;
    for (auto x : PairList) {
      std::cout << std::get<0>(x) << std::endl;
    }

    throw std::runtime_error("Invalid field: " + name);
  }

  auto gep =
      cc.Builder->CreateStructGEP(ST, BASE.ActualValueButAsAPointer, index);

  auto loaded = cc.Builder->CreateLoad(FieldType, gep);

  // return {loaded, gep, BASE.ActualType, BASE.ActualTypeButNotThePointer};
  llvm::Type *NoPtr = nullptr;

  if (FieldType->isPointerTy()) {

    if (auto ST =
            llvm::dyn_cast<llvm::StructType>(BASE.ActualTypeButNotThePointer)) {

      auto it = cc.StructsToPair.find(ST);

      if (it != cc.StructsToPair.end()) {
        for (auto &x : it->second) {
          if (std::get<0>(x) == name) {
            NoPtr = std::get<2>(x);

            if (NoPtr->isPointerTy())
              NoPtr = nullptr;

            break;
          }
        }
      }
    }

  } else {
    NoPtr = FieldType;
  }

  return {loaded, gep, FieldType, NoPtr};
}

CodegenResults PointerFieldAccessNode::codegen(CodegenContext &cc) {
  CodegenResults BASE = base->codegen(cc);

  llvm::Value *BasePtr = BASE.ActualValue;

  if (!BasePtr || !BasePtr->getType()->isPointerTy()) {
    throw std::runtime_error("PointerFieldAccess: base is not a pointer value");
  }

  llvm::Type *PointeeTy = BASE.ActualTypeButNotThePointer;
  if (!PointeeTy) {
    throw std::runtime_error("PointerFieldAccess: base pointee type is null");
  }

  auto ST = llvm::dyn_cast<llvm::StructType>(PointeeTy);
  if (!ST) {
    throw std::runtime_error(
        "PointerFieldAccess: field access on non-struct pointer");
  }

  auto it = cc.StructsToPair.find(ST);
  if (it == cc.StructsToPair.end()) {
    throw std::runtime_error("PointerFieldAccess: unknown struct type");
  }

  const auto &PairList = it->second;

  size_t index = (size_t)-1;
  llvm::Type *FieldType = nullptr;

  for (auto &x : PairList) {
    if (std::get<0>(x) == name) {
      index = std::get<1>(x);
      FieldType = std::get<2>(x);
      break;
    }
  }

  if (index == (size_t)-1) {
    std::cerr << "Available Fields\n";
    for (auto &x : PairList) {
      std::cout << std::get<0>(x) << "\n";
    }
    throw std::runtime_error("Invalid field: " + name);
  }

  llvm::Value *gep = cc.Builder->CreateStructGEP(ST, BasePtr, index);
  llvm::Value *loaded = cc.Builder->CreateLoad(FieldType, gep);

  llvm::Type *NoPtr = nullptr;
  if (FieldType->isPointerTy()) {
    NoPtr = nullptr;
  } else {
    NoPtr = FieldType;
  }

  return {loaded, gep, FieldType, NoPtr};
}
