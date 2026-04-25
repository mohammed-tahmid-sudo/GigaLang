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
#include <llvm-18/llvm/IR/Intrinsics.h>
#include <llvm-18/llvm/IR/LLVMContext.h>
#include <llvm-18/llvm/IR/Metadata.h>
#include <llvm-18/llvm/IR/Type.h>
#include <llvm-18/llvm/IR/Value.h>
#include <llvm-18/llvm/IR/Verifier.h>
#include <llvm-18/llvm/Support/TypeName.h>
#include <llvm-18/llvm/Support/raw_ostream.h>
#include <memory>
#include <stdexcept>
#include <strings.h>
#include <utility>
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
    auto it = cc.StructIndexList.find(type.value)->second;
    return it;
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
  for (char &c : type.value)
    c = toupper(c);

  if (type.value == "VOID")
    return llvm::Type::getVoidTy(*cc.TheContext);

  return GetTypeNonVoid(type, cc);
}

CodegenResult CharNode::codegen(CodegenContext &cc) {
  return {
      llvm::ConstantInt::get(llvm::Type::getInt8Ty(*cc.TheContext), val, false),
      llvm::Type::getInt8Ty(*cc.TheContext)};
}

CodegenResult IntegerNode::codegen(CodegenContext &cc) {
  return {
      llvm::ConstantInt::get(llvm::Type::getInt32Ty(*cc.TheContext), val, true),
      llvm::Type::getInt32Ty(*cc.TheContext)};
}

CodegenResult FloatNode::codegen(CodegenContext &cc) {

  return {
      llvm::ConstantInt::get(llvm::Type::getFloatTy(*cc.TheContext), val, true),
      llvm::Type::getFloatTy(*cc.TheContext)};
}

CodegenResult BooleanNode::codegen(CodegenContext &cc) {
  return {
      llvm::ConstantInt::get(llvm::Type::getInt1Ty(*cc.TheContext), val, true),
      llvm::Type::getInt1Ty(*cc.TheContext)};
}

CodegenResult VariableDeclareNode::codegen(CodegenContext &cc) {
  llvm::Type *elementType = GetTypeNonVoid(Type, cc);
  llvm::AllocaInst *alloca = nullptr;

  if (arraySize.has_value() && *arraySize > 1) {
    llvm::ArrayType *arrayType = llvm::ArrayType::get(elementType, *arraySize);
    alloca = cc.Builder->CreateAlloca(arrayType, nullptr, name);

    if (val) {
      ArrayLiteralNode *arrayNode = dynamic_cast<ArrayLiteralNode *>(val.get());
      if (arrayNode) {
        for (size_t i = 0; i < arrayNode->Elements.size(); ++i) {
          CodegenResult elemRes = arrayNode->Elements[i]->codegen(cc);

          llvm::Value *gep = cc.Builder->CreateGEP(
              arrayType, alloca,
              {cc.Builder->getInt32(0), cc.Builder->getInt32(i)}, "elemptr");

          cc.Builder->CreateStore(elemRes.value, gep);
        }
      }
    } else {
      for (unsigned i = 0; i < *arraySize; ++i) {
        llvm::Value *gep = cc.Builder->CreateGEP(
            arrayType, alloca,
            {cc.Builder->getInt32(0), cc.Builder->getInt32(i)});
        llvm::Value *zero = llvm::Constant::getNullValue(elementType);
        cc.Builder->CreateStore(zero, gep);
      }
    }
  } else {
    alloca = cc.Builder->CreateAlloca(elementType, nullptr, name);

    llvm::Value *initVal;
    if (val) {
      initVal = val->codegen(cc).value;
    } else {
      initVal = llvm::Constant::getNullValue(elementType);
    }

    cc.Builder->CreateStore(initVal, alloca);
  }

  cc.addVariable(name, alloca, alloca->getType(), elementType);

  return {alloca, elementType};
}

CodegenResult AssignmentNode::codegen(CodegenContext &cc) {
  llvm::Value *destAddr = nullptr;

  // 1. If LHS is a variable, bypass its codegen() to get the raw address
  if (auto *varRef = dynamic_cast<VariableReferenceNode *>(lhs.get())) {
    destAddr = cc.lookup(varRef->Name);
  } else {
    CodegenResult lhsRes = lhs->codegen(cc);
    destAddr = lhsRes.value;
  }

  if (!destAddr) {
    llvm::errs() << "Error: invalid LHS in assignment!\n";
    return {nullptr, nullptr};
  }

  // 3. Get the RHS result (the "what").
  CodegenResult rhsRes = rhs->codegen(cc);
  if (!rhsRes.value) {
    llvm::errs() << "Error in assignment: RHS expression returned null!\n";
    return {nullptr, nullptr};
  }

  // 4. Perform the store: store <value>, <pointer>
  cc.Builder->CreateStore(rhsRes.value, destAddr);

  return rhsRes;
}

CodegenResult ReturnNode::codegen(CodegenContext &cc) {
  if (expr) {
    // 1. Get the address/value and the type
    CodegenResult retRes = expr->codegen(cc);

    if (!retRes.value)
      return {nullptr, nullptr};

    // 2. Emit the LLVM return instruction using .val
    llvm::Value *retInst = cc.Builder->CreateRet(retRes.value);

    // 3. Return the result of the expression
    return {retInst, retRes.type};
  } else {
    // Void return
    llvm::Value *retInst = cc.Builder->CreateRetVoid();
    return {retInst, llvm::Type::getVoidTy(*cc.TheContext)};
  }
}

CodegenResult CompoundNode::codegen(CodegenContext &cc) {
  CodegenResult last = {nullptr};

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

CodegenResult FunctionNode::codegen(CodegenContext &cc) {
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

  CodegenResult retVal = content->codegen(cc);

  llvm::BasicBlock *currentBB = cc.Builder->GetInsertBlock();
  if (!currentBB->getTerminator()) {
    if (retTy->isVoidTy()) {
      cc.Builder->CreateRetVoid();
    } else {
      if (!retVal.value) {
        Fn->eraseFromParent();
        cc.popScope();
        return {nullptr, nullptr};
      }
      cc.Builder->CreateRet(retVal.value);
    }
  }

  llvm::verifyFunction(*Fn);
  cc.popScope();
  return {Fn, retTy};
}

CodegenResult VariableReferenceNode::codegen(CodegenContext &cc) {
  llvm::Value *ptr = cc.lookup(Name); // The address (alloca)
  llvm::Type *ty =
      cc.lookupElementType(Name); // The actual data type (i32, Struct, etc.)

  if (!ptr)
    throw std::runtime_error("Unknown variable: " + Name);

  return {cc.Builder->CreateLoad(ty, ptr, Name), ty};
}

CodegenResult WhileNode::codegen(CodegenContext &cc) {
  llvm::Function *F = cc.Builder->GetInsertBlock()->getParent();
  llvm::LLVMContext &Ctx = *cc.TheContext;

  llvm::BasicBlock *condBB = llvm::BasicBlock::Create(Ctx, "while.cond", F);
  llvm::BasicBlock *bodyBB = llvm::BasicBlock::Create(Ctx, "while.body", F);
  llvm::BasicBlock *afterBB = llvm::BasicBlock::Create(Ctx, "while.end", F);

  cc.Builder->CreateBr(condBB);

  cc.Builder->SetInsertPoint(condBB);
  CodegenResult condVal = condition->codegen(cc);
  if (!condVal.value)
    return {nullptr, nullptr};

  if (!condVal.value->getType()->isIntegerTy(1)) {
    condVal.value = cc.Builder->CreateICmpNE(
        condVal.value, llvm::ConstantInt::get(condVal.value->getType(), 0),
        "while.cond.to.i1");
  }
  cc.Builder->CreateCondBr(condVal.value, bodyBB, afterBB);
  cc.Builder->SetInsertPoint(bodyBB);

  llvm::BasicBlock *oldBreak = cc.BreakBB;
  llvm::BasicBlock *oldCont = cc.ContinueBB;
  cc.BreakBB = afterBB;
  cc.ContinueBB = condBB;

  if (!body->codegen(cc).value) {
    cc.BreakBB = oldBreak;
    cc.ContinueBB = oldCont;
    return {nullptr, nullptr};
  }

  cc.BreakBB = oldBreak;
  cc.ContinueBB = oldCont;

  if (!cc.Builder->GetInsertBlock()->getTerminator())
    cc.Builder->CreateBr(condBB);

  cc.Builder->SetInsertPoint(afterBB);

  return {nullptr, nullptr};
}

CodegenResult IfNode::codegen(CodegenContext &cc) {
  CodegenResult condV = condition->codegen(cc);
  if (!condV.value)
    return {nullptr, nullptr};

  condV.value = cc.Builder->CreateICmpNE(
      condV.value, llvm::ConstantInt::get(condV.value->getType(), 0), "ifcond");
  llvm::Function *func = cc.Builder->GetInsertBlock()->getParent();

  llvm::BasicBlock *thenBB =
      llvm::BasicBlock::Create(*cc.TheContext, "then", func);
  llvm::BasicBlock *elseBB =
      elseBlock ? llvm::BasicBlock::Create(*cc.TheContext, "else", func)
                : nullptr;
  llvm::BasicBlock *mergeBB =
      llvm::BasicBlock::Create(*cc.TheContext, "ifcont", func);

  if (elseBB)
    cc.Builder->CreateCondBr(condV.value, thenBB, elseBB);
  else
    cc.Builder->CreateCondBr(condV.value, thenBB, mergeBB);

  cc.Builder->SetInsertPoint(thenBB);
  cc.pushScope();
  thenBlock->codegen(cc);
  cc.popScope();
  if (!cc.Builder->GetInsertBlock()->getTerminator()) // ← ADD THIS CHECK
    cc.Builder->CreateBr(mergeBB);

  if (elseBB) {
    cc.Builder->SetInsertPoint(elseBB);
    cc.pushScope();
    elseBlock->codegen(cc);
    cc.popScope();
    if (!cc.Builder->GetInsertBlock()->getTerminator()) // ← AND THIS
      cc.Builder->CreateBr(mergeBB);
  }

  cc.Builder->SetInsertPoint(mergeBB);
  return {nullptr, nullptr};
}

CodegenResult autoLoad(CodegenContext &cc, CodegenResult res) {
  if (!res.value)
    return res;

  if (res.value->getType()->isPointerTy() && res.type != res.value->getType()) {
    return {cc.Builder->CreateLoad(res.type, res.value), res.type};
  }
  return res;
}

CodegenResult BinaryOperationNode::codegen(CodegenContext &cc) {
  // 1. Get raw results from children
  CodegenResult LHS = Left->codegen(cc);
  CodegenResult RHS = Right->codegen(cc);

  if (!LHS.value || !RHS.value)
    throw std::runtime_error("null operand in binary operation");

  // 2. Convert addresses (L-Values) into values (R-Values)
  LHS = autoLoad(cc, LHS);
  RHS = autoLoad(cc, RHS);

  // 3. Unify Types (Promotion)
  auto *i32 = llvm::Type::getInt32Ty(*cc.TheContext);
  auto *i1 = llvm::Type::getInt1Ty(*cc.TheContext);

  // Promote booleans/chars to i32 for arithmetic
  if (LHS.value->getType()->isIntegerTy(1) ||
      LHS.value->getType()->isIntegerTy(8)) {
    LHS.value = cc.Builder->CreateIntCast(LHS.value, i32, true);
    LHS.type = i32;
  }
  if (RHS.value->getType()->isIntegerTy(1) ||
      RHS.value->getType()->isIntegerTy(8)) {
    RHS.value = cc.Builder->CreateIntCast(RHS.value, i32, true);
    RHS.type = i32;
  }

  // Ensure LHS and RHS match
  if (LHS.value->getType() != RHS.value->getType()) {
    if (LHS.value->getType()->isIntegerTy() &&
        RHS.value->getType()->isIntegerTy()) {
      RHS.value =
          cc.Builder->CreateIntCast(RHS.value, LHS.value->getType(), true);
      RHS.type = LHS.type;
    } else {
      // Log types for debugging
      llvm::errs() << "LHS: ";
      LHS.value->getType()->print(llvm::errs());
      llvm::errs() << "\nRHS: ";
      RHS.value->getType()->print(llvm::errs());
      throw std::runtime_error(
          "\nCannot perform arithmetic on incompatible types");
    }
  }

  // 4. Generate Instructions
  switch (Type) {
  case TokenType::PLUS:
    return {cc.Builder->CreateAdd(LHS.value, RHS.value, "addtmp"), LHS.type};
  case TokenType::MINUS:
    return {cc.Builder->CreateSub(LHS.value, RHS.value, "subtmp"), LHS.type};
  case TokenType::STAR:
    return {cc.Builder->CreateMul(LHS.value, RHS.value, "multmp"), LHS.type};
  case TokenType::SLASH:
    return {cc.Builder->CreateSDiv(LHS.value, RHS.value, "divtmp"), LHS.type};

  case TokenType::EQEQ:
    return {cc.Builder->CreateICmpEQ(LHS.value, RHS.value, "eqtmp"), i1};
  case TokenType::NOTEQ:
    return {cc.Builder->CreateICmpNE(LHS.value, RHS.value, "netmp"), i1};
  case TokenType::GT:
    return {cc.Builder->CreateICmpSGT(LHS.value, RHS.value, "gttmp"), i1};
  case TokenType::LT:
    return {cc.Builder->CreateICmpSLT(LHS.value, RHS.value, "lttmp"), i1};
  case TokenType::GTE:
    return {cc.Builder->CreateICmpSGE(LHS.value, RHS.value, "gtetmp"), i1};
  case TokenType::LTE:
    return {cc.Builder->CreateICmpSLE(LHS.value, RHS.value, "ltetmp"), i1};

  case TokenType::AND: {
    // Logic operations always return i1
    return {cc.Builder->CreateAnd(LHS.value, RHS.value, "andtmp"), i1};
  }

  default:
    throw std::runtime_error("Unknown binary operator");
  }
}

CodegenResult BreakNode::codegen(CodegenContext &cc) {
  if (!cc.BreakBB) {
    std::cerr << "Error: 'break' not inside a loop.\n";
    return {nullptr, nullptr};
  }
  return {cc.Builder->CreateBr(cc.BreakBB),
          cc.Builder->CreateBr(cc.BreakBB)->getType()};
}

CodegenResult CallNode::codegen(CodegenContext &cc) {
  llvm::Function *callee = cc.Module->getFunction(name);
  if (!callee) {
    llvm::errs() << "Error: Unknown function referenced: " << name << "\n";
    return {nullptr, nullptr};
  }

  std::vector<llvm::Value *> argRawVals;
  for (auto &arg : args) {
    CodegenResult res = arg->codegen(cc);
    if (!res.value) {
      llvm::errs() << "Error: Null argument in call to " << name << "\n";
      return {nullptr, nullptr};
    }
    argRawVals.push_back(res.value);
  }

  llvm::CallInst *call = cc.Builder->CreateCall(callee, argRawVals);

  return {call, call->getType()};
}

CodegenResult ContinueNode::codegen(CodegenContext &cc) {

  if (!cc.BreakBB) {
    std::cerr << "Error: 'break' not inside a loop.\n";
    return {nullptr, nullptr};
  }
  return {cc.Builder->CreateBr(cc.ContinueBB),
          cc.Builder->CreateBr(cc.ContinueBB)->getType()};
}

CodegenResult ForNode::codegen(CodegenContext &cc) {
  llvm::Function *function = cc.Builder->GetInsertBlock()->getParent();

  // 1. Initializer
  if (init) {
    init->codegen(cc);
  }

  llvm::BasicBlock *loopCondBB =
      llvm::BasicBlock::Create(*cc.TheContext, "loopcond", function);
  llvm::BasicBlock *loopBodyBB =
      llvm::BasicBlock::Create(*cc.TheContext, "loopbody", function);
  llvm::BasicBlock *loopEndBB =
      llvm::BasicBlock::Create(*cc.TheContext, "loopend", function);

  // Jump to condition
  cc.Builder->CreateBr(loopCondBB);
  cc.Builder->SetInsertPoint(loopCondBB);

  // 2. Condition
  CodegenResult condRes = condition->codegen(cc);
  if (!condRes.value)
    return {nullptr, nullptr};

  // Convert result to a boolean (i1) for the branch instruction
  // We use .val and .type from our result
  llvm::Value *condBool = cc.Builder->CreateICmpNE(
      condRes.value, llvm::ConstantInt::get(condRes.value->getType(), 0),
      "forcond");

  cc.Builder->CreateCondBr(condBool, loopBodyBB, loopEndBB);

  // 3. Body
  cc.Builder->SetInsertPoint(loopBodyBB);

  // Track break/continue blocks if your context supports them
  llvm::BasicBlock *oldBreak = cc.BreakBB;
  llvm::BasicBlock *oldCont = cc.ContinueBB;
  cc.BreakBB = loopEndBB;
  cc.ContinueBB = loopCondBB; // Or an increment block if you had one

  if (body)
    body->codegen(cc);

  // 4. Increment
  if (increment)
    increment->codegen(cc);

  cc.Builder->CreateBr(loopCondBB);

  // 5. Cleanup
  cc.Builder->SetInsertPoint(loopEndBB);
  cc.BreakBB = oldBreak;
  cc.ContinueBB = oldCont;

  // Loops usually return void
  return {nullptr, llvm::Type::getVoidTy(*cc.TheContext)};
}

CodegenResult ArrayLiteralNode::codegen(CodegenContext &cc) {
  // 1. Ensure there is at least one element to determine type
  if (Elements.empty()) {
    std::cerr << "Error: ArrayLiteralNode has no elements\n";
    return {nullptr, nullptr};
  }

  // 2. Determine the element type from the first element
  CodegenResult firstElem = Elements[0]->codegen(cc);
  if (!firstElem.value) {
    std::cerr << "Error: Could not generate code for array element\n";
    return {nullptr, nullptr};
  }

  // Use the type we manually tracked in the result
  llvm::Type *ElementType = firstElem.type;

  // 3. Create array type and allocate memory
  llvm::ArrayType *arrType = llvm::ArrayType::get(ElementType, Elements.size());
  llvm::AllocaInst *arrayAlloc =
      cc.Builder->CreateAlloca(arrType, nullptr, "arraytmp");

  // 4. Store each element in the allocated array
  for (size_t i = 0; i < Elements.size(); i++) {
    CodegenResult elemRes = Elements[i]->codegen(cc);
    if (!elemRes.value) {
      std::cerr << "Error: Could not generate code for element at index " << i
                << "\n";
      continue;
    }

    // GEP returns a Value* (the address of the specific index)
    llvm::Value *gep = cc.Builder->CreateGEP(
        arrType, arrayAlloc, {cc.Builder->getInt32(0), cc.Builder->getInt32(i)},
        "elemptr");

    // Store the value into that address
    cc.Builder->CreateStore(elemRes.value, gep);
  }

  // 5. Return the address of the array and the ArrayType itself
  return {arrayAlloc, arrType};
}

CodegenResult ArrayAccessNode::codegen(CodegenContext &cc) {
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

  CodegenResult indexVal = indexExpr->codegen(cc);
  if (!indexVal.value->getType()->isIntegerTy())
    throw std::runtime_error("Array index must be integer");

  if (indexVal.value->getType() != builder.getInt32Ty())
    indexVal.value =
        builder.CreateIntCast(indexVal.value, builder.getInt32Ty(), true);

  llvm::Value *elemPtr = builder.CreateGEP(
      arrayType, arrayPtr, {builder.getInt32(0), indexVal.value},
      arrayName + "_elem_ptr");

  llvm::Type *elementType = arrayType->getArrayElementType();

  // Important: return the *loaded value*, not just the pointer
  return {
      builder.CreateLoad(elementType, elemPtr, arrayName + "_elem"),
      builder.CreateLoad(elementType, elemPtr, arrayName + "_elem")->getType()};
}

CodegenResult ArrayAssignNode::codegen(CodegenContext &cc) {

  llvm::Value *arrayVal = cc.lookup(name);
  if (!arrayVal)
    throw std::runtime_error("Undefined array variable: " + name);

  llvm::AllocaInst *alloca = llvm::dyn_cast<llvm::AllocaInst>(arrayVal);
  if (!alloca)
    throw std::runtime_error(name + " is not stack allocated");

  llvm::Type *arrayType = alloca->getAllocatedType();
  if (!arrayType->isArrayTy())
    throw std::runtime_error(name + " is not an array");

  CodegenResult index = this->index->codegen(cc);
  if (!index.value)
    throw std::runtime_error("Invalid index expression in array assignment");

  if (!index.value->getType()->isIntegerTy())
    throw std::runtime_error("Array index must be integer");

  if (index.value->getType() != llvm::Type::getInt32Ty(*cc.TheContext))
    index.value = cc.Builder->CreateIntCast(
        index.value, llvm::Type::getInt32Ty(*cc.TheContext), true, "idxcast");

  llvm::Value *zero =
      llvm::ConstantInt::get(llvm::Type::getInt32Ty(*cc.TheContext), 0);

  llvm::Value *elemPtr = cc.Builder->CreateGEP(
      arrayType, alloca, {zero, index.value}, name + "_elem_ptr");

  CodegenResult val = value->codegen(cc);
  if (!val.value)
    throw std::runtime_error("Invalid RHS in array assignment");

  llvm::Type *elemType = arrayType->getArrayElementType();
  if (val.value->getType() != elemType)
    throw std::runtime_error("Type mismatch in array assignment");

  cc.Builder->CreateStore(val.value, elemPtr);

  return val;
}
CodegenResult SizeOfNode::codegen(CodegenContext &cc) {
  if (!val) {
    throw std::runtime_error("INVALID VALUE AT SIZEOF NODE");
  }
  CodegenResult valRes = val->codegen(cc);
  llvm::Type *targetType = valRes.type;

  if (!targetType) {
    throw std::runtime_error("Could not determine type for sizeof");
  }

  const llvm::DataLayout &DL = cc.Module->getDataLayout();
  uint64_t sizeInBytes = DL.getTypeAllocSize(targetType);

  llvm::Type *i64Ty = llvm::Type::getInt64Ty(*cc.TheContext);
  llvm::Value *sizeVal = llvm::ConstantInt::get(i64Ty, sizeInBytes);

  return {sizeVal, i64Ty};
}

CodegenResult castToI64(CodegenResult v, CodegenContext &cc,
                        const std::string &varName = "") {
  llvm::Type *i64Ty = llvm::Type::getInt64Ty(*cc.TheContext);

  // Already i64
  if (v.value->getType()->isIntegerTy(64))
    return v;

  // Single integer smaller than 64-bit → extend
  if (v.value->getType()->isIntegerTy())
    return {cc.Builder->CreateZExt(v.value, i64Ty),
            cc.Builder->CreateZExt(v.value, i64Ty)->getType()};

  // Pointer or array stored in context
  if (!varName.empty()) {
    llvm::Type *type = cc.lookupType(varName);
    llvm::Type *elemType = cc.lookupElementType(varName);

    if (type && elemType) {
      if (elemType->isIntegerTy(8)) {
        // Arrays or pointers of i8 → cast to integer
        if (type->isArrayTy())
          v.value = cc.Builder->CreateBitCast(
              v.value, llvm::PointerType::get(
                           llvm::Type::getInt8Ty(*cc.TheContext), false));
        return {cc.Builder->CreatePtrToInt(v.value, i64Ty),
                cc.Builder->CreatePtrToInt(v.value, i64Ty)->getType()};
      }
    }
  }

  // Float → integer
  if (v.value->getType()->isFloatingPointTy())
    return {cc.Builder->CreateFPToUI(v.value, i64Ty),
            cc.Builder->CreateFPToUI(v.value, i64Ty)->getType()};

  llvm_unreachable("Unsupported type for syscall argument");
}

CodegenResult SyscallNode::codegen(CodegenContext &cc) {
  llvm::Type *i64Ty = llvm::Type::getInt64Ty(*cc.TheContext);
  std::vector<llvm::Value *> llvm_args;

  for (auto &arg : args) {
    CodegenResult res = arg->codegen(cc);
    if (!res.value)
      continue;

    llvm::Value *val64;
    if (res.value->getType()->isPointerTy()) {
      val64 = cc.Builder->CreatePtrToInt(res.value, i64Ty);
    } else {
      val64 = cc.Builder->CreateZExtOrBitCast(res.value, i64Ty);
    }
    llvm_args.push_back(val64);
  }

  while (llvm_args.size() < 6) {
    llvm_args.push_back(llvm::ConstantInt::get(i64Ty, 0));
  }

  llvm::Value *syscallNumVal = llvm::ConstantInt::get(i64Ty, name);

  std::vector<llvm::Value *> final_raw_args = {syscallNumVal};
  final_raw_args.insert(final_raw_args.end(), llvm_args.begin(),
                        llvm_args.begin() + 6);

  llvm::InlineAsm *asmSyscall = llvm::InlineAsm::get(
      llvm::FunctionType::get(i64Ty, std::vector<llvm::Type *>(7, i64Ty),
                              false),
      "syscall",
      "={rax},{rax},{rdi},{rsi},{rdx},{r10},{r8},{r9},~{rcx},~{r11},~{memory}",
      true);

  llvm::Value *result =
      cc.Builder->CreateCall(asmSyscall, final_raw_args, "syscall_res");

  return {result, i64Ty};
}

CodegenResult PointerReferenceNode::codegen(CodegenContext &cc) {
  llvm::Value *var = cc.lookup(name);

  if (!var) {
    throw std::runtime_error("CANNOT FIND VALUE " + name);
  }
  return {var, var->getType()};
}

CodegenResult PointerDeReferenceAssingNode::codegen(CodegenContext &cc) {
  llvm::Value *ptrVariableAddr = cc.lookup(name);
  if (!ptrVariableAddr)
    throw std::runtime_error("Unknown pointer variable: " + name);

  llvm::Type *ptrVarType =
      cc.lookupType(name); // The type of the variable (usually ptr)
  llvm::Type *elementType =
      cc.lookupElementType(name); // The type of data being pointed to

  llvm::Value *actualPtr =
      cc.Builder->CreateLoad(ptrVarType, ptrVariableAddr, name + "_val");

  CodegenResult idxRes = index->codegen(cc);
  if (!idxRes.value)
    throw std::runtime_error("Invalid index for pointer access");

  llvm::Value *elemPtr =
      cc.Builder->CreateGEP(elementType, actualPtr, {idxRes.value}, "ptr_elem");

  CodegenResult valueRes = val->codegen(cc);
  if (!valueRes.value)
    throw std::runtime_error("Invalid RHS value in pointer assignment");

  cc.Builder->CreateStore(valueRes.value, elemPtr);

  return valueRes;
}

CodegenResult DeReferenceNode::codegen(CodegenContext &cc) {
  // 1. Get the address of the pointer variable (the alloca)
  llvm::Value *varAddr = cc.lookup(name);
  if (!varAddr) {
    llvm::errs() << "Unknown variable '" << name << "'\n";
    return {nullptr, nullptr};
  }

  // 2. Get the types from context
  llvm::Type *ptrType = cc.lookupType(name);
  llvm::Type *elementType = cc.lookupElementType(name);

  if (!ptrType) {
    throw std::runtime_error("NullPointer exception in compiler logic for " +
                             name);
  }

  // 3. Load the actual address stored in the pointer variable
  llvm::Value *actualPtr =
      cc.Builder->CreateLoad(ptrType, varAddr, name + "_ptr");

  // 4. Handle indexing (e.g., ptr[i])
  if (index) {
    CodegenResult idxRes = index->codegen(cc);
    if (!idxRes.value)
      return {nullptr, nullptr};

    // GEP to calculate the specific offset
    actualPtr = cc.Builder->CreateGEP(elementType, actualPtr, {idxRes.value},
                                      "ptr_elem");
  }

  // 5. Final Load: Read the data at that address
  llvm::Value *finalVal =
      cc.Builder->CreateLoad(elementType, actualPtr, "deref_" + name);

  // Return using .value
  return {finalVal, elementType};
}

CodegenResult castValue(llvm::IRBuilder<> &builder, CodegenResult res,
                        llvm::Type *targetType, bool isSigned) {
  // 1. Extract the raw LLVM Value and Type from the struct
  llvm::Value *val = res.value;
  llvm::Type *srcType = res.type; // Use the tracked type, not val->getType()

  if (!val || !srcType)
    return {nullptr, nullptr};
  if (srcType == targetType)
    return res;

  llvm::Value *resultVal = nullptr;

  // ===== Integer ↔ Integer =====
  if (srcType->isIntegerTy() && targetType->isIntegerTy()) {
    resultVal = builder.CreateIntCast(val, targetType, isSigned);
  }
  // ===== Integer → Float =====
  else if (srcType->isIntegerTy() && targetType->isFloatingPointTy()) {
    resultVal = isSigned ? builder.CreateSIToFP(val, targetType)
                         : builder.CreateUIToFP(val, targetType);
  }
  // ===== Float → Integer =====
  else if (srcType->isFloatingPointTy() && targetType->isIntegerTy()) {
    resultVal = isSigned ? builder.CreateFPToSI(val, targetType)
                         : builder.CreateFPToUI(val, targetType);
  }
  // ===== Float ↔ Float =====
  else if (srcType->isFloatingPointTy() && targetType->isFloatingPointTy()) {
    resultVal = builder.CreateFPCast(val, targetType);
  }
  // ===== Pointer ↔ Pointer =====
  else if (srcType->isPointerTy() && targetType->isPointerTy()) {
    // In opaque pointer LLVM, bitcast between pointers is often a no-op
    // but kept here for compatibility or specific address spaces.
    resultVal = builder.CreateBitCast(val, targetType);
  }
  // ===== Pointer ↔ Integer =====
  else if (srcType->isPointerTy() && targetType->isIntegerTy()) {
    resultVal = builder.CreatePtrToInt(val, targetType);
  } else if (srcType->isIntegerTy() && targetType->isPointerTy()) {
    resultVal = builder.CreateIntToPtr(val, targetType);
  }

  if (resultVal) {
    return {resultVal, targetType};
  }

  llvm::errs() << "Unsupported cast requested\n";
  return {nullptr, nullptr};
}

CodegenResult CastNode::codegen(CodegenContext &cc) {
  CodegenResult v = Value->codegen(cc);
  return castValue(*cc.Builder, v, targetType, true);
}

CodegenResult StructCreateNode::codegen(CodegenContext &cc) {
  // 1. Create the opaque struct type
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

  cc.CreateStructWithIndex(name, TheStruct, indexs);

  return {nullptr, nullptr};
}

CodegenResult FieldAccessNode::codegen(CodegenContext &cc) {
  CodegenResult base = Base->codegen(cc);
  if (!base.value)
    return {nullptr, nullptr};

  if (!base.type->isStructTy())
    return {nullptr, nullptr};

  auto *structTy = llvm::cast<llvm::StructType>(base.type);

  auto it = cc.StructToIndex.find(structTy);
  if (it == cc.StructToIndex.end())
    return {nullptr, nullptr};

  const auto &idx = it->second; // <-- you missed this

  int fieldIndex = -1;
  for (const auto &[fieldName, index] : idx) {
    if (fieldName == name) {
      fieldIndex = index;
      break;
    }
  }

  if (fieldIndex == -1)
    return {nullptr, nullptr};

  llvm::Value *ptr =
      cc.Builder->CreateStructGEP(structTy, base.value, fieldIndex);

  llvm::Type *fieldTy = structTy->getElementType(fieldIndex);

  llvm::Value *val = cc.Builder->CreateLoad(fieldTy, ptr);

  return {val, fieldTy};
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
