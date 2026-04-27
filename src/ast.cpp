#include <ast.h>
#include <lexer.h>
#include <llvm-18/llvm/IR/Function.h>
#include <sstream>
#include <string>

std::string IntegerNode::repr() {
  return "IntegerNode(" + std::to_string(val) + ")";
}

std::string FloatNode::repr() {
  return "FloatNode(" + std::to_string(val) + ")";
}

std::string BooleanNode::repr() {
  return "BooleanNode(" + std::to_string(val) + ")";
}

std::string CharNode::repr() {
  return "CharNode(" + std::to_string(static_cast<int>(val)) + ")";
}

// std::string StringNode::repr() { return "StringNode(" + val + ")"; }

std::string VariableDeclareNode::repr() { return "VariableDeclareNode"; }

std::string AssignmentNode::repr() {
  // return "AssignmentNode(name=" + name + ", NewValue=" + val->repr() + ")";
  return "AssignmentNode";
}

std::string CompoundNode::repr() {
  std::string output = "[";
  bool first = true;
  for (auto &block : blocks) {
    if (!first)
      output += ", ";
    output += block->repr();
    first = false;
  }
  output += "]";
  return output;
}

std::string FunctionNode::repr() {
  return "FunctionNode(Name=" + name + ", Value=[" + content->repr() +
         "], ReturnType=" + ReturnType.value + ")";
}

std::string VariableReferenceNode::repr() {
  return "VariableReferenceNode(" + Name + ")";
}

std::string WhileNode::repr() {
  return "WhileNode(Condition=" + condition->repr() + ", Body=" + body->repr() +
         ")";
}

std::string IfNode::repr() {
  return "IfNode(Condition=" + condition->repr() +
         ", ThenBlock=" + thenBlock->repr() +
         ", ElseBlock=" + (elseBlock ? elseBlock->repr() : "null") + ")";
}

std::string ReturnNode::repr() { return "ReturnNode(" + expr->repr() + ")"; }

std::string BinaryOperationNode::repr() {
  std::ostringstream oss;
  oss << "BinaryOperationNode(Op=" << tokenName(Type)
      << ", Left=" << Left->repr() << ", Right=" << Right->repr() << ")";
  return oss.str();
}

std::string BreakNode::repr() { return "BreakNode()"; }

std::string ContinueNode::repr() { return "ContinueNode()"; }

std::string CallNode::repr() {
  std::string s = "CallNode(Name=" + name + ", Contents=[";
  bool first = true;
  for (auto &arg : args) {
    if (!first)
      s += ", ";
    s += arg->repr();
    first = false;
  }
  s += "])";
  return s;
}

std::string ForNode::repr() {
  std::string s = "ForNode(init=" + (init ? init->repr() : "null") +
                  ", condition=" + (condition ? condition->repr() : "null") +
                  ", increment=" + (increment ? increment->repr() : "null") +
                  ", body=" + (body ? body->repr() : "null") + ")";
  return s;
}

std::string ArrayLiteralNode::repr() {
  std::string s = "ArrayLiteralNode([";
  bool first = true;
  for (auto &elem : Elements) {
    if (!first)
      s += ", ";
    s += elem->repr();
    first = false;
  }
  s += "])";
  return s;
}

std::string ArrayAccessNode::repr() {
  return "ArrayAccessNode(Name=" + arrayName + ", Index=" + indexExpr->repr() +
         ")";
}

std::string ArrayAssignNode::repr() {
  return "ArrayAssignNode(Name=" + name + ", Index=" + index->repr() +
         ", Value=" + value->repr() + ")";
}

std::string SizeOfNode::repr() { return "SizeOfNode(" + val->repr() + ")"; }

std::string PointerReferenceNode::repr() { return "PointerReferenceNode"; }

std::string PointerDeReferenceAssingNode::repr() {
  return "PointerDeReferenceAssingNode=" + name + ", Index=" + index->repr() +
         ", Value=" + val->repr() + ")";
}
