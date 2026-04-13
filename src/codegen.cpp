#include "lexer.h"
#include "llvm/IR/InlineAsm.h"
#include <alloca.h>
#include <ast.h>
#include <cctype>
#include <iostream>
#include <llvm-18/llvm/IR/BasicBlock.h>
#include <llvm-18/llvm/IR/Constants.h>
#include <llvm-18/llvm/IR/DerivedTypes.h>
#include <llvm-18/llvm/IR/Function.h>
#include <llvm-18/llvm/IR/Instructions.h>
#include <llvm-18/llvm/IR/LLVMContext.h>
#include <llvm-18/llvm/IR/Metadata.h>
#include <llvm-18/llvm/IR/Type.h>
#include <llvm-18/llvm/IR/Value.h>
#include <llvm-18/llvm/IR/Verifier.h>
#include <llvm-18/llvm/Support/TypeName.h>
#include <llvm-18/llvm/Support/raw_ostream.h>
#include <memory>
#include <stdexcept>
#include <vector>

llvm::Type *GetPointeeType(Token typeToken, llvm::LLVMContext &context) {
  std::string t = typeToken.value;
  for (char &c : t)
    c = toupper(c);

  if (t.size() > 7 && t.substr(t.size() - 7) == "POINTER") {
    Token baseToken;
    baseToken.value = t.substr(0, t.size() - 7); // strip "POINTER"
    return GetTypeNonVoid(baseToken, context); // "CHAR" -> i8, "INTEGER" -> i32
  }
  return nullptr;
}

llvm::Type *GetTypeNonVoid(Token type, llvm::LLVMContext &context) {
  std::string t = type.value;

  for (char &c : t)
    c = toupper(c);

  if (t.size() > 7 && t.substr(t.size() - 7) == "POINTER") {
    Token baseTypeToken;
    baseTypeToken.value = t.substr(0, t.size() - 7);
    llvm::Type *baseType = GetTypeNonVoid(baseTypeToken, context);
    return llvm::PointerType::get(baseType, 0);
  }

  if (t == "INTEGER") {
    return llvm::Type::getInt32Ty(context);
  } else if (t == "FLOAT") {
    return llvm::Type::getFloatTy(context);
  } else if (t == "STRING") {
    return llvm::Type::getInt8Ty(context);
  } else if (t == "BOOLEAN") {
    return llvm::Type::getInt1Ty(context);
  } else if (t == "CHAR") {
    return llvm::Type::getInt8Ty(context);
  }

  throw std::runtime_error("Invalid Type: " + type.value);
  return nullptr;
}

llvm::Type *GetTypeVoid(Token type, llvm::LLVMContext &context) {
  for (char &c : type.value)
    c = toupper(c);

  if (type.value == "VOID")
    return llvm::Type::getVoidTy(context);

  return GetTypeNonVoid(type, context);
}

llvm::Value *CharNode::codegen(CodegenContext &cc) {
  return llvm::ConstantInt::get(llvm::Type::getInt8Ty(*cc.TheContext), val,
                                false);
}

llvm::Value *IntegerNode::codegen(CodegenContext &cc) {
  return llvm::ConstantInt::get(llvm::Type::getInt32Ty(*cc.TheContext), val,
                                true);
}

llvm::Value *FloatNode::codegen(CodegenContext &cc) {

  return llvm::ConstantFP::get(llvm::Type::getFloatTy(*cc.TheContext), val);
}

llvm::Value *BooleanNode::codegen(CodegenContext &cc) {
  return llvm::ConstantInt::get(llvm::Type::getInt1Ty(*cc.TheContext), val,
                                true);
}

// llvm::Value *StringNode::codegen(CodegenContext &cc) {
//   return llvm::ConstantDataArray::getString(*cc.TheContext, val,
//                                             true); // true = add null
//                                             terminator
// }

llvm::Value *VariableDeclareNode::codegen(CodegenContext &cc) {
  llvm::Type *elementType = GetTypeNonVoid(Type, *cc.TheContext);
  llvm::AllocaInst *alloca = nullptr;

  if (!cc.Builder->GetInsertBlock())
    std::cout << "NO INSERT BLOCK\n";

  if (arraySize.has_value() && *arraySize > 1) {
    llvm::ArrayType *arrayType = llvm::ArrayType::get(elementType, *arraySize);
    alloca = cc.Builder->CreateAlloca(arrayType, nullptr, name);

    if (val) {
      ArrayLiteralNode *arrayNode = dynamic_cast<ArrayLiteralNode *>(val.get());
      if (arrayNode) {
        for (size_t i = 0; i < arrayNode->Elements.size(); ++i) {
          llvm::Value *elemVal = arrayNode->Elements[i]->codegen(cc);
          llvm::Value *gep = cc.Builder->CreateGEP(
              arrayType, alloca,
              {cc.Builder->getInt32(0), cc.Builder->getInt32(i)}, "elemptr");
          cc.Builder->CreateStore(elemVal, gep);
        }
      }
    } else {
      for (unsigned i = 0; i < *arraySize; ++i) {
        llvm::Value *gep = cc.Builder->CreateGEP(
            arrayType, alloca,
            {cc.Builder->getInt32(0), cc.Builder->getInt32(i)});
        llvm::Value *zero = llvm::ConstantInt::get(elementType, 0);
        cc.Builder->CreateStore(zero, gep);
      }
    }

  } else {
    // Scalar or single-element array treated as scalar
    alloca = cc.Builder->CreateAlloca(elementType, nullptr, name);
    llvm::Value *initVal =
        val ? val->codegen(cc) : llvm::Constant::getNullValue(elementType);
    cc.Builder->CreateStore(initVal, alloca);
  }

  llvm::Type *pointeeType = nullptr;
  if (elementType->isPointerTy()) {
    Token baseTypeToken;
    baseTypeToken.value =
        Type.value.substr(0, Type.value.size() - 7); // strip "POINTER"
    pointeeType = GetTypeNonVoid(baseTypeToken, *cc.TheContext);
  }
  cc.addVariable(name, alloca, elementType, pointeeType);
  return alloca;
}

llvm::Value *AssignmentNode::codegen(CodegenContext &cc) {
  llvm::Value *var = cc.lookup(name);
  if (!var) {
    llvm::errs() << "Error: variable '" << name << "' not declared!\n";
    return nullptr; // prevents cast crash
  }

  llvm::Value *valueVal = val->codegen(cc);
  if (!valueVal) {
    llvm::errs() << "Error IN ASSINGMENT NODE: RHS expression returned null!\n";
    return nullptr;
  }

  // Store the value into the existing alloca
  return cc.Builder->CreateStore(valueVal, var);
}

llvm::Value *ReturnNode::codegen(CodegenContext &cc) {
  if (expr) {
    llvm::Value *retVal = expr->codegen(cc);
    return cc.Builder->CreateRet(retVal);
  } else {
    return cc.Builder->CreateRetVoid();
  }
}

llvm::Value *CompoundNode::codegen(CodegenContext &cc) {
  llvm::Value *last = nullptr;

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

llvm::Value *FunctionNode::codegen(CodegenContext &cc) {
  std::vector<llvm::Type *> argTypes;
  for (auto &a : args)
    argTypes.push_back(std::get<1>(a)); // was a.second

  llvm::Type *retTy = GetTypeVoid(ReturnType, *cc.TheContext);
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

  llvm::Value *retVal = content->codegen(cc);

  llvm::BasicBlock *currentBB = cc.Builder->GetInsertBlock();
  if (!currentBB->getTerminator()) {
    if (retTy->isVoidTy()) {
      cc.Builder->CreateRetVoid();
    } else {
      if (!retVal) {
        Fn->eraseFromParent();
        cc.popScope();
        return nullptr;
      }
      cc.Builder->CreateRet(retVal);
    }
  }

  llvm::verifyFunction(*Fn);
  cc.popScope();
  return Fn;
}

llvm::Value *VariableReferenceNode::codegen(CodegenContext &cc) {
  llvm::Value *var = cc.lookup(Name);
  if (!var)
    throw std::runtime_error("Unknown variable: " + Name);

  if (!var->getType()->isPointerTy())
    return var;

  llvm::errs() << "Var type: ";
  var->getType()->print(llvm::errs());
  llvm::errs() << "\n";

  llvm::Type *elemTy = nullptr;

  if (auto *AI = llvm::dyn_cast<llvm::AllocaInst>(var))
    elemTy = AI->getAllocatedType();
  else if (auto *GV = llvm::dyn_cast<llvm::GlobalVariable>(var))
    elemTy = GV->getValueType();
  else if (auto *GEP = llvm::dyn_cast<llvm::GetElementPtrInst>(var))
    elemTy = GEP->getResultElementType();
  else
    throw std::runtime_error("Cannot determine element type of variable: " +
                             Name);

  return cc.Builder->CreateLoad(elemTy, var, Name);
}

// llvm::Value *VariableReferenceNode::codegen(CodegenContext &cc) {
//   llvm::Value *var = cc.lookup(Name);
//   if (!var)
//     throw std::runtime_error("Unknown variable: " + Name);

//   llvm::errs() << "Variable: " << Name << "\n";
//   llvm::errs() << "LLVM type: ";
//   var->getType()->print(llvm::errs());
//   llvm::errs() << "\n";

//   llvm::errs() << "Full value: ";
//   var->print(llvm::errs());
//   llvm::errs() << "\n";

//   llvm::Type *type;
//   if (auto *AI = llvm::dyn_cast<llvm::AllocaInst>(var)) {
//     type = AI->getAllocatedType(); // safe in LLVM 18+
//   } else {
//     throw std::runtime_error("Variable is not an alloca: " + Name);
//   }

//   return cc.Builder->CreateLoad(type, var, Name);
// }

llvm::Value *WhileNode::codegen(CodegenContext &cc) {
  llvm::Function *F = cc.Builder->GetInsertBlock()->getParent();
  llvm::LLVMContext &Ctx = *cc.TheContext;

  llvm::BasicBlock *condBB = llvm::BasicBlock::Create(Ctx, "while.cond", F);
  llvm::BasicBlock *bodyBB = llvm::BasicBlock::Create(Ctx, "while.body", F);
  llvm::BasicBlock *afterBB = llvm::BasicBlock::Create(Ctx, "while.end", F);

  cc.Builder->CreateBr(condBB);

  cc.Builder->SetInsertPoint(condBB);
  llvm::Value *condVal = condition->codegen(cc);
  if (!condVal)
    return nullptr;

  if (!condVal->getType()->isIntegerTy(1)) {
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

  if (!body->codegen(cc)) {
    cc.BreakBB = oldBreak;
    cc.ContinueBB = oldCont;
    return nullptr;
  }

  cc.BreakBB = oldBreak;
  cc.ContinueBB = oldCont;

  if (!cc.Builder->GetInsertBlock()->getTerminator())
    cc.Builder->CreateBr(condBB);

  cc.Builder->SetInsertPoint(afterBB);

  // return llvm::Constant::getNullValue(llvm::Type::getInt32Ty(Ctx));
  return nullptr;
}

llvm::Value *IfNode::codegen(CodegenContext &cc) {
  llvm::Value *condV = condition->codegen(cc);
  if (!condV)
    return nullptr;

  condV = cc.Builder->CreateICmpNE(
      condV, llvm::ConstantInt::get(condV->getType(), 0), "ifcond");

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

  // --- then ---
  cc.Builder->SetInsertPoint(thenBB);
  cc.pushScope();
  thenBlock->codegen(cc);
  cc.popScope();
  if (!cc.Builder->GetInsertBlock()->getTerminator()) // ← ADD THIS CHECK
    cc.Builder->CreateBr(mergeBB);

  // --- else ---
  if (elseBB) {
    cc.Builder->SetInsertPoint(elseBB);
    cc.pushScope();
    elseBlock->codegen(cc);
    cc.popScope();
    if (!cc.Builder->GetInsertBlock()->getTerminator()) // ← AND THIS
      cc.Builder->CreateBr(mergeBB);
  }

  // --- merge ---
  cc.Builder->SetInsertPoint(mergeBB);
  return nullptr;
}

// llvm::Value *IfNode::codegen(CodegenContext &cc) {
//   llvm::Value *condV = condition->codegen(cc);
//   if (!condV)
//     return nullptr;

//   // bool conversion
//   condV = cc.Builder->CreateICmpNE(
//       condV, llvm::ConstantInt::get(condV->getType(), 0), "ifcond");

//   llvm::Function *func = cc.Builder->GetInsertBlock()->getParent();

//   // create blocks
//   llvm::BasicBlock *thenBB =
//       llvm::BasicBlock::Create(*cc.TheContext, "then", func);
//   llvm::BasicBlock *elseBB = nullptr;
//   if (elseBlock)
//     elseBB = llvm::BasicBlock::Create(*cc.TheContext, "else", func);

//   llvm::BasicBlock *mergeBB =
//       llvm::BasicBlock::Create(*cc.TheContext, "ifcont", func);

//   // conditional branch
//   if (elseBB)
//     cc.Builder->CreateCondBr(condV, thenBB, elseBB);
//   else
//     cc.Builder->CreateCondBr(condV, thenBB, mergeBB);

//   // then
//   cc.Builder->SetInsertPoint(thenBB);
//   cc.pushScope();
//   thenBlock->codegen(cc);
//   cc.popScope();
//   cc.Builder->CreateBr(mergeBB);
//   thenBB = cc.Builder->GetInsertBlock();

//   // else (if present)
//   if (elseBB) {
//     cc.Builder->SetInsertPoint(elseBB);
//     cc.pushScope();
//     elseBlock->codegen(cc);
//     cc.popScope();
//     cc.Builder->CreateBr(mergeBB);
//     elseBB = cc.Builder->GetInsertBlock();
//   }

//   // merge
//   cc.Builder->SetInsertPoint(mergeBB);
//   return nullptr;
// }

llvm::Value *BinaryOperationNode::codegen(CodegenContext &cc) {
  llvm::Value *LHS = Left->codegen(cc);
  llvm::Value *RHS = Right->codegen(cc);

  if (!LHS || !RHS)
    throw std::runtime_error("null operand in binary operation");

  LHS->getType()->print(llvm::errs());
  llvm::errs() << "\n";
  RHS->getType()->print(llvm::errs());
  llvm::errs() << "\n";

  switch (Type) {

  case TokenType::PLUS:
  case TokenType::MINUS:
  case TokenType::STAR:
  case TokenType::SLASH: {
    // If either operand is i1, promote to i32
    auto *i32 = llvm::Type::getInt32Ty(*cc.TheContext);
    if (LHS->getType()->isIntegerTy(1))
      LHS = cc.Builder->CreateIntCast(LHS, i32, true);
    if (RHS->getType()->isIntegerTy(1))
      RHS = cc.Builder->CreateIntCast(RHS, i32, true);

    // If types still mismatch, cast RHS to match LHS
    if (LHS->getType() != RHS->getType()) {
      if (LHS->getType()->isIntegerTy() && RHS->getType()->isIntegerTy()) {
        RHS = cc.Builder->CreateIntCast(RHS, LHS->getType(), true);
      } else {
        throw std::runtime_error(
            "Cannot perform arithmetic on incompatible types");
      }
    }

    if (Type == TokenType::PLUS)
      return cc.Builder->CreateAdd(LHS, RHS, "addtmp");
    if (Type == TokenType::MINUS)
      return cc.Builder->CreateSub(LHS, RHS, "subtmp");
    if (Type == TokenType::STAR)
      return cc.Builder->CreateMul(LHS, RHS, "multmp");
    if (Type == TokenType::SLASH)
      return cc.Builder->CreateSDiv(LHS, RHS, "divtmp");
  }

  case TokenType::EQEQ: {
    if (LHS->getType() != RHS->getType()) {
      if (LHS->getType()->isIntegerTy() && RHS->getType()->isIntegerTy()) {
        RHS = cc.Builder->CreateIntCast(RHS, LHS->getType(), true);
      } else {
        throw std::runtime_error("Cannot compare incompatible types");
      }
    }

    return cc.Builder->CreateICmpEQ(LHS, RHS, "eqtmp");
  }

  case TokenType::NOTEQ: {
    if (LHS->getType() != RHS->getType()) {
      if (LHS->getType()->isIntegerTy() && RHS->getType()->isIntegerTy()) {
        RHS = cc.Builder->CreateIntCast(RHS, LHS->getType(), true);
      } else {
        throw std::runtime_error("Cannot compare incompatible types");
      }
    }

    return cc.Builder->CreateICmpNE(LHS, RHS, "netmp");
  }
  case TokenType::AND: {
    // Convert LHS to i1 if needed
    if (!LHS->getType()->isIntegerTy(1))
      LHS = cc.Builder->CreateICmpNE(
          LHS, llvm::ConstantInt::get(LHS->getType(), 0), "lhsbool");

    // Convert RHS to i1 if needed
    if (!RHS->getType()->isIntegerTy(1))
      RHS = cc.Builder->CreateICmpNE(
          RHS, llvm::ConstantInt::get(RHS->getType(), 0), "rhsbool");

    return cc.Builder->CreateAnd(LHS, RHS, "andtmp");
  }
  case TokenType::GTE: {
    if (LHS->getType() != RHS->getType()) {
      if (LHS->getType()->isIntegerTy() && RHS->getType()->isIntegerTy()) {
        RHS = cc.Builder->CreateIntCast(RHS, LHS->getType(), true);
      } else {
        throw std::runtime_error("Cannot compare incompatible types");
      }
    }
    return cc.Builder->CreateICmpSGE(LHS, RHS,
                                     "gtetmp"); // signed greater or equal
  }

  case TokenType::LTE: {
    if (LHS->getType() != RHS->getType()) {
      if (LHS->getType()->isIntegerTy() && RHS->getType()->isIntegerTy()) {
        RHS = cc.Builder->CreateIntCast(RHS, LHS->getType(), true);
      } else {
        throw std::runtime_error("Cannot compare incompatible types");
      }
    }
    return cc.Builder->CreateICmpSLE(LHS, RHS,
                                     "ltetmp"); // signed less or equal
  }

  case TokenType::GT: {
    if (LHS->getType() != RHS->getType()) {
      if (LHS->getType()->isIntegerTy() && RHS->getType()->isIntegerTy()) {
        RHS = cc.Builder->CreateIntCast(RHS, LHS->getType(), true);
      } else {
        throw std::runtime_error("Cannot compare incompatible types");
      }
    }
    return cc.Builder->CreateICmpSGT(LHS, RHS, "gttmp"); // signed greater than
  }

  case TokenType::LT: {
    if (LHS->getType() != RHS->getType()) {
      if (LHS->getType()->isIntegerTy() && RHS->getType()->isIntegerTy()) {
        RHS = cc.Builder->CreateIntCast(RHS, LHS->getType(), true);
      } else {
        throw std::runtime_error("Cannot compare incompatible types");
      }
    }
    return cc.Builder->CreateICmpSLT(LHS, RHS, "lttmp"); // signed less than
  }

  default:
    throw std::runtime_error("Unknown binary operator " +
                             std::string(tokenName(Type)));
  }
}

// llvm::Value *BinaryOperationNode::codegen(CodegenContext &cc) {
//   llvm::Value *LHS = Left->codegen(cc);
//   llvm::Value *RHS = Right->codegen(cc);

//   if (!LHS || !RHS)
//     throw std::runtime_error("null operand in binary operation");

//   // force both operands to i32
//   auto *i32 = llvm::Type::getInt32Ty(*cc.TheContext);

//   if (LHS->getType()->isIntegerTy(1))
//     LHS = cc.Builder->CreateIntCast(LHS, i32, true);

//   if (RHS->getType()->isIntegerTy(1))
//     RHS = cc.Builder->CreateIntCast(RHS, i32, true);

//   switch (Type) {
//   case TokenType::PLUS:
//     return cc.Builder->CreateAdd(LHS, RHS, "addtmp");

//   case TokenType::MINUS:
//     return cc.Builder->CreateSub(LHS, RHS, "subtmp");

//   case TokenType::STAR:
//     return cc.Builder->CreateMul(LHS, RHS, "multmp");

//   case TokenType::SLASH:
//     return cc.Builder->CreateSDiv(LHS, RHS, "divtmp");

//   case TokenType::LT: {
//     auto *cmp = cc.Builder->CreateICmpSLT(LHS, RHS, "lttmp");
//     return cc.Builder->CreateIntCast(cmp, i32, true);
//   }

//   case TokenType::LTE: {
//     auto *cmp = cc.Builder->CreateICmpSLE(LHS, RHS, "letmp");
//     return cc.Builder->CreateIntCast(cmp, i32, true);
//   }

//   case TokenType::GT: {
//     auto *cmp = cc.Builder->CreateICmpSGT(LHS, RHS, "gttmp");
//     return cc.Builder->CreateIntCast(cmp, i32, true);
//   }

//   case TokenType::GTE: {
//     auto *cmp = cc.Builder->CreateICmpSGE(LHS, RHS, "getmp");
//     return cc.Builder->CreateIntCast(cmp, i32, true);
//   }

//   case TokenType::EQEQ: {
//     auto *cmp = cc.Builder->CreateICmpEQ(LHS, RHS, "eqtmp");
//     return cc.Builder->CreateIntCast(cmp, i32, true);
//   }

//   case TokenType::NOTEQ: {
//     auto *cmp = cc.Builder->CreateICmpNE(LHS, RHS, "netmp");
//     return cc.Builder->CreateIntCast(cmp, i32, true);
//   }

//   default:
//     throw std::runtime_error("unknown binary operator Named" +
//                              std::string(tokenName(Type)));
//   }
// }

llvm::Value *BreakNode::codegen(CodegenContext &cc) {
  if (!cc.BreakBB) {
    std::cerr << "Error: 'break' not inside a loop.\n";
    return nullptr;
  }
  return cc.Builder->CreateBr(cc.BreakBB);
}

llvm::Value *CallNode::codegen(CodegenContext &cc) {
  llvm::Function *callee = cc.Module->getFunction(name);
  if (!callee)
    return nullptr;

  //   if (callee->arg_size() != args.size())
  //     return nullptr;

  std::vector<llvm::Value *> argVals;
  for (auto &arg : args) {
    llvm::Value *v = arg->codegen(cc);
    if (!v)
      return nullptr;
    argVals.push_back(v);
  }

  return cc.Builder->CreateCall(callee, argVals);
}

llvm::Value *ContinueNode::codegen(CodegenContext &cc) {

  if (!cc.BreakBB) {
    std::cerr << "Error: 'break' not inside a loop.\n";
    return nullptr;
  }
  return cc.Builder->CreateBr(cc.ContinueBB);
}

llvm::Value *ForNode::codegen(CodegenContext &cc) {
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

  cc.Builder->CreateBr(loopCondBB);
  cc.Builder->SetInsertPoint(loopCondBB);
  llvm::Value *condValue = condition->codegen(cc);
  if (!condValue)
    return nullptr;

  condValue = cc.Builder->CreateICmpNE(
      condValue, llvm::ConstantInt::get(condValue->getType(), 0), "forcond");

  cc.Builder->CreateCondBr(condValue, loopBodyBB, loopEndBB);

  cc.Builder->SetInsertPoint(loopBodyBB);
  if (body)
    body->codegen(cc);

  if (increment)
    increment->codegen(cc);

  cc.Builder->CreateBr(loopCondBB);

  cc.Builder->SetInsertPoint(loopEndBB);

  // return
  // llvm::Constant::getNullValue(llvm::Type::getInt32Ty(*cc.TheContext));
  return nullptr;
}

llvm::Value *ArrayLiteralNode::codegen(CodegenContext &cc) {
  // Ensure there is at least one element
  if (Elements.empty()) {
    std::cerr << "Error: ArrayLiteralNode has no elements\n";
    return nullptr;
  }

  // Determine the element type from the first element
  llvm::Value *firstElem = Elements[0]->codegen(cc);
  if (!firstElem) {
    std::cerr << "Error: Could not generate code for array element\n";
    return nullptr;
  }

  llvm::Type *ElementType = firstElem->getType();
  if (!ElementType) {
    std::cerr << "Error: ElementType is null\n";
    return nullptr;
  }

  // Create array type
  llvm::ArrayType *arrType = llvm::ArrayType::get(ElementType, Elements.size());
  llvm::AllocaInst *arrayAlloc =
      cc.Builder->CreateAlloca(arrType, nullptr, "arraytmp");

  // Store each element in the allocated array
  for (size_t i = 0; i < Elements.size(); i++) {
    llvm::Value *elemVal = Elements[i]->codegen(cc);
    if (!elemVal) {
      std::cerr << "Error: Could not generate code for element at index " << i
                << "\n";
      continue;
    }

    llvm::Value *gep = cc.Builder->CreateGEP(
        arrType, arrayAlloc, {cc.Builder->getInt32(0), cc.Builder->getInt32(i)},
        "elemptr");

    cc.Builder->CreateStore(elemVal, gep);
  }

  return arrayAlloc; // return pointer to the allocated array
}

llvm::Value *ArrayAccessNode::codegen(CodegenContext &cc) {
  llvm::Value *arrayPtr = cc.lookup(arrayName);

  if (!arrayPtr)
    throw std::runtime_error("Unknown array: " + arrayName);

  llvm::Type *arrayType = nullptr;

  if (auto *allocaInst = llvm::dyn_cast<llvm::AllocaInst>(arrayPtr)) {
    arrayType = allocaInst->getAllocatedType();
  } else if (auto *globalVar = llvm::dyn_cast<llvm::GlobalVariable>(arrayPtr)) {
    arrayType = globalVar->getValueType();
  } else {
    throw std::runtime_error(arrayName + " is not a valid array variable");
  }

  if (!arrayType->isArrayTy())
    throw std::runtime_error(arrayName + " is not an array");

  llvm::IRBuilder<> &builder = *cc.Builder;

  llvm::Value *indexVal = indexExpr->codegen(cc);
  if (!indexVal->getType()->isIntegerTy())
    throw std::runtime_error("Array index must be integer");

  if (indexVal->getType() != builder.getInt32Ty())
    indexVal = builder.CreateIntCast(indexVal, builder.getInt32Ty(), true);

  llvm::Value *elemPtr =
      builder.CreateGEP(arrayType, arrayPtr, {builder.getInt32(0), indexVal},
                        arrayName + "_elem_ptr");

  llvm::Type *elementType = arrayType->getArrayElementType();

  // Important: return the *loaded value*, not just the pointer
  return builder.CreateLoad(elementType, elemPtr, arrayName + "_elem");
}

llvm::Value *ArrayAssignNode::codegen(CodegenContext &cc) {

  llvm::Value *arrayVal = cc.lookup(name);
  if (!arrayVal)
    throw std::runtime_error("Undefined array variable: " + name);

  llvm::AllocaInst *alloca = llvm::dyn_cast<llvm::AllocaInst>(arrayVal);
  if (!alloca)
    throw std::runtime_error(name + " is not stack allocated");

  llvm::Type *arrayType = alloca->getAllocatedType();
  if (!arrayType->isArrayTy())
    throw std::runtime_error(name + " is not an array");

  llvm::Value *index = this->index->codegen(cc);
  if (!index)
    throw std::runtime_error("Invalid index expression in array assignment");

  if (!index->getType()->isIntegerTy())
    throw std::runtime_error("Array index must be integer");

  if (index->getType() != llvm::Type::getInt32Ty(*cc.TheContext))
    index = cc.Builder->CreateIntCast(
        index, llvm::Type::getInt32Ty(*cc.TheContext), true, "idxcast");

  llvm::Value *zero =
      llvm::ConstantInt::get(llvm::Type::getInt32Ty(*cc.TheContext), 0);

  llvm::Value *elemPtr = cc.Builder->CreateGEP(arrayType, alloca, {zero, index},
                                               name + "_elem_ptr");

  llvm::Value *val = value->codegen(cc);
  if (!val)
    throw std::runtime_error("Invalid RHS in array assignment");

  llvm::Type *elemType = arrayType->getArrayElementType();
  if (val->getType() != elemType)
    throw std::runtime_error("Type mismatch in array assignment");

  cc.Builder->CreateStore(val, elemPtr);

  return val;
}

llvm::Value *SizeOfNode::codegen(CodegenContext &cc) {
  if (!val) {
    throw std::runtime_error("INVALUD VALUE AT SIZEOF NODE");
    return nullptr;
  }
  // uint32_t type =
  //     cc.Module->getDataLayout().getTypeAllocSize(val->codegen(cc)->getType());

  return llvm::ConstantExpr::getSizeOf(val->codegen(cc)->getType());
}

llvm::Value *castToI64(llvm::Value *v, CodegenContext &cc,
                       const std::string &varName = "") {
  llvm::Type *i64Ty = llvm::Type::getInt64Ty(*cc.TheContext);

  // Already i64
  if (v->getType()->isIntegerTy(64))
    return v;

  // Single integer smaller than 64-bit → extend
  if (v->getType()->isIntegerTy())
    return cc.Builder->CreateZExt(v, i64Ty);

  // Pointer or array stored in context
  if (!varName.empty()) {
    llvm::Type *type = cc.lookupType(varName);
    llvm::Type *elemType = cc.lookupElementType(varName);

    if (type && elemType) {
      if (elemType->isIntegerTy(8)) {
        // Arrays or pointers of i8 → cast to integer
        if (type->isArrayTy())
          v = cc.Builder->CreateBitCast(
              v, llvm::PointerType::get(llvm::Type::getInt8Ty(*cc.TheContext),
                                        false));
        return cc.Builder->CreatePtrToInt(v, i64Ty);
      }
    }
  }

  // Float → integer
  if (v->getType()->isFloatingPointTy())
    return cc.Builder->CreateFPToUI(v, i64Ty);

  llvm_unreachable("Unsupported type for syscall argument");
}

llvm::Value *SyscallNode::codegen(CodegenContext &cc) {
  llvm::Type *i64Ty = llvm::Type::getInt64Ty(*cc.TheContext);
  std::vector<llvm::Value *> llvm_args;

  // Generate code and cast each arg to i64
  for (auto &arg : args) {
    llvm::Value *v = arg->codegen(cc);
    llvm_args.push_back(v);
  }

  // Zero-pad to 6 arguments
  while (llvm_args.size() < 6)
    llvm_args.push_back(llvm::ConstantInt::get(i64Ty, 0));

  // Syscall number
  llvm::Value *syscall_num = llvm::ConstantInt::get(i64Ty, name);

  // Final argument list: syscall number first
  std::vector<llvm::Value *> final_args = {syscall_num};
  final_args.insert(final_args.end(), llvm_args.begin(), llvm_args.begin() + 6);

  llvm::InlineAsm *asmSyscall = llvm::InlineAsm::get(
      llvm::FunctionType::get(i64Ty, std::vector<llvm::Type *>(7, i64Ty),
                              false),
      "syscall",
      "={rax},{rax},{rdi},{rsi},{rdx},{r10},{r8},{r9},~{rcx},~{r11},~{memory}",
      true // hasSideEffects
  );

  return cc.Builder->CreateCall(asmSyscall, final_args);
}

llvm::Value *PointerReferenceNode::codegen(CodegenContext &cc) {
  llvm::Value *var = cc.lookup(name);

  if (!var) {
    throw std::runtime_error("CANNOT FIND VALUE " + name);
  }
  return var;
}

llvm::Value *PointerDeReferenceAssingNode::codegen(CodegenContext &cc) {
  llvm::Value *arrayVal = cc.lookup(name);
  if (!arrayVal)
    throw std::runtime_error("Unknown pointer array: " + name);

  llvm::Type *ptrType = cc.lookupType(name);
  llvm::Type *elemType = cc.lookupElementType(name);

  llvm::Value *actualPtr =
      cc.Builder->CreateLoad(ptrType, arrayVal, name + "_ptr");

  llvm::Value *idx = index->codegen(cc);
  llvm::Value *elemPtr =
      cc.Builder->CreateGEP(elemType, actualPtr, {idx}, "ptr_elem");

  llvm::Value *value = val->codegen(cc);
  return cc.Builder->CreateStore(value, elemPtr);
}

llvm::Value *DeReferenceNode::codegen(CodegenContext &cc) {
  llvm::Value *var = cc.lookup(name);
  if (!var) {
    llvm::errs() << "Unknown variable '" << name << "'\n";
    return nullptr;
  }
  llvm::Type *ptrType = cc.lookupType(name);

  if (nullptr == ptrType) {
    throw std::runtime_error("NullPointer, Baby");
  }
  if (!ptrType || !ptrType->isPointerTy()) {
    llvm::errs() << "'" << name << "' is not a pointer\n";
    return nullptr;
  }

  llvm::Value *ptrVal = cc.Builder->CreateLoad(ptrType, var, name + "_ptr");
  llvm::Type *elementType = cc.lookupElementType(name);

  llvm::errs() << "DeReferenceNode '" << name << "' elementType: ";
  elementType->print(llvm::errs());
  llvm::errs() << "\n";

  // If there's an index, apply GEP before loading
  if (index) {
    llvm::Value *idx = index->codegen(cc);
    ptrVal = cc.Builder->CreateGEP(elementType, ptrVal, {idx}, "ptr_elem");
  }

  return cc.Builder->CreateLoad(elementType, ptrVal, "deref_" + name);
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

llvm::Value *CastNode::codegen(CodegenContext &cc) {
  llvm::Value *v = Value->codegen(cc);
  return castValue(*cc.Builder, v, targetType, true);
}

llvm::Value *StructCreateNode::codegen(CodegenContext &cc) {
  llvm::LLVMContext &context = *cc.TheContext;

  // LLVM 15+ way to lookup struct
  llvm::StructType *structType = llvm::StructType::getTypeByName(context, name);

  if (!structType) {
    // Create new struct
    structType = llvm::StructType::create(context, name);
    structType->setBody(types, false);
  } else {
    // Handle forward declaration
    if (structType->isOpaque()) {
      structType->setBody(types, false);
    }
  }

  // Store in your context (type, not value)
  cc.addVariable(name, nullptr, structType, nullptr);

  return nullptr; // no runtime value
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
