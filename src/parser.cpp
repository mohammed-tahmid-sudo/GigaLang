#include "ast.h"
#include "lexer.h"
#include <cctype>
#include <colors.h>
#include <iomanip>
#include <iostream>
#include <llvm-18/llvm/IR/DerivedTypes.h>
#include <llvm-18/llvm/IR/InstrTypes.h>
#include <llvm-18/llvm/IR/Instruction.h>
#include <llvm-18/llvm/IR/Intrinsics.h>
#include <llvm-18/llvm/IR/PassManager.h>
#include <llvm-18/llvm/IR/Type.h>
#include <llvm-18/llvm/Support/CommandLine.h>
#include <llvm-18/llvm/Support/Error.h>
#include <llvm-18/llvm/Support/MathExtras.h>
#include <llvm-18/llvm/Support/raw_ostream.h>
#include <memory>
#include <parser.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

Token Parser::Peek() {
  if (x < input.size()) {
    return input[x];
  }
  return {TokenType::EOF_TOKEN, ""};
}

Token Parser::Consume() {
  if (x < input.size()) {
    return input[x++]; // return current, then advance
  }
  throw std::runtime_error("Attempted to consume past end of input");
}

Token Parser::Expect(TokenType tk) {
  if (Peek().type == tk) {
    // std::cout << "COMMING HERE " << tokenName(tk) << std::endl;
    return Consume();
  }
  throw std::runtime_error("EXPECTED " + std::string(tokenName(tk)) + ", GOT " +
                           tokenName(Peek().type));
}

std::unique_ptr<ast> Parser::ParseFactor() {
  if (Peek().type == TokenType::INT_LITERAL) {
    int val = std::stoi(Peek().value);
    Consume();
    return std::make_unique<IntegerNode>(val);

  } else if (Peek().type == TokenType::FLOAT_LITERAL) {
    float val = std::stof(Peek().value);
    Consume();
    return std::make_unique<FloatNode>(val);

  } else if (Peek().type == TokenType::BOOLEAN_LITERAL) {

    Token tok = Peek();

    for (auto &val : tok.value) {
      val = std::toupper(static_cast<unsigned char>(val));
    }

    if (tok.value == "TRUE") {
      Consume();
      return std::make_unique<BooleanNode>(true);
    }

    Consume();
    return std::make_unique<BooleanNode>(false);

  } else if (Peek().type == STRING_LITERAL) {
    std::string val = Peek().value;
    Consume();
    if (val.size() == 1) { // treat single-character strings as Char
      return std::make_unique<CharNode>(val[0]);
    }

    // multi-character strings become arrays
    std::vector<std::unique_ptr<ast>> outputs;
    for (auto &tok : val) {
      outputs.push_back(std::make_unique<CharNode>(tok));
    }
    if (!outputs.empty() &&
        static_cast<CharNode *>(outputs.back().get())->val != '\0') {
      outputs.push_back(std::make_unique<CharNode>(0));
    }
    return std::make_unique<ArrayLiteralNode>(
        llvm::Type::getInt32Ty(*cc.TheContext), std::move(outputs));
  }

  else if (Peek().type == TokenType::LPAREN) {
    Expect(TokenType::LPAREN);
    auto val = ParseExpression();
    if (!val) {
      throw std::runtime_error("VAL IS A NULLPTR");
    }
    Expect(TokenType::RPAREN);
    return val;
  } else if (Peek().type == TokenType::LBRACKET) {
    Expect(TokenType::LBRACKET);
    std::vector<std::unique_ptr<ast>> elements;

    while (Peek().type != TokenType::RBRACKET) {
      if (Peek().type == TokenType::EOF_TOKEN) {
        throw std::runtime_error("Unexpected EOF in array literal");
      }
      elements.push_back(ParseExpression());
      if (Peek().type == TokenType::COMMA) {
        Consume();
      } else {
        break;
      }
    }

    Expect(TokenType::RBRACKET);
    return std::make_unique<ArrayLiteralNode>(nullptr, std::move(elements));

  } else if (Peek().type == TokenType::IDENTIFIER) {
    Token name = Peek();
    Consume();
    if (Peek().type == EQ) {
      Consume();
      auto val = ParseExpression();

      // kxpect(SEMICOLON);

      return std::make_unique<AssignmentNode>(name.value, std::move(val));

    } else if (Peek().type == LPAREN) {

      Consume();
      std::vector<std::unique_ptr<ast>> args;

      while (Peek().type != RPAREN) {
        if (Peek().type == EOF_TOKEN) {
          throw std::runtime_error(
              "Unexpected end of input while parsing function arguments for " +
              name.value);
        }
        auto arg = ParseExpression();
        args.push_back(std::move(arg));
        if (Peek().type == COMMA) {
          Consume();
        } else {
          break;
        }
      }

      Expect(RPAREN);

      return std::make_unique<CallNode>(name.value, std::move(args));
    } else if (Peek().type == LBRACKET) {
      Consume();
      auto val = ParseExpression();
      // if (!dynamic_cast<IntegerNode *>(val.get())) {
      //   throw std::runtime_error("Expected a Number, But got Something
      //   Else");
      // }
      Expect(RBRACKET);

      return std::make_unique<ArrayAccessNode>(name.value, std::move(val));

      return std::make_unique<SizeOfNode>(std::move(val));

    } else {
      return std::make_unique<VariableReferenceNode>(name.value);
    }
  } else if (Peek().type == CHAR_LITERAL) {
    Token val = Consume();
    if (val.value.size() < 1) {
      throw std::runtime_error("Expected a single value as a char but got" +
                               std::to_string(val.value.size()));
    }
    return std::make_unique<CharNode>(val.value[0]);
  } else if (Peek().type == SIZEOF) {
    Consume();
    Expect(LPAREN);
    auto val = ParseExpression();
    Expect(RPAREN);

    return std::make_unique<SizeOfNode>(std::move(val));
  } else if (Peek().type == SYSCALL) {
    Consume();
    Expect(LPAREN);
    Token name = Expect(STRING_LITERAL);
    Expect(COMMA);
    std::vector<std::unique_ptr<ast>> args;
    while (Peek().type != RPAREN) {
      args.push_back(ParseExpression());

      if (Peek().type == COMMA) {
        Consume();
        continue;
      } else {
        break;
      }
    }

    Expect(RPAREN);

    return std::make_unique<SyscallNode>(name.value, std::move(args));
  } else if (Peek().type == ANDPERCENT) {
    Consume();
    Token name = Expect(IDENTIFIER);
    return std::make_unique<PointerReferenceNode>(name.value);

  } else if (Peek().type == STAR) {
    Consume();
    if (Peek().type == IDENTIFIER) {
      Token v = Peek();
      Consume();
      return std::make_unique<DeReferenceNode>(v.value);
    }
    std::cerr << "Huh";
    return nullptr;

  } else {
    if (Peek().type == SEMICOLON) {
      return nullptr;
    } else {
      throw std::runtime_error("Unexpected token in ParseFactor: " +
                               std::string(tokenName(Peek().type)));
    }
  }
}

std::unique_ptr<ast> Parser::ParseTerm() {
  std::unique_ptr<ast> left = ParseFactor();
  while (Peek().type == TokenType::STAR || Peek().type == TokenType::SLASH ||
         Peek().type == TokenType::GT || Peek().type == GTE ||
         Peek().type == LT || Peek().type == LTE || Peek().type == EQEQ ||
         Peek().type == AND) {
    TokenType type = Peek().type;
    Consume();

    std::unique_ptr<ast> right = ParseFactor();

    if (!right)
      throw std::runtime_error("EXPECTED A NUMBER AFTER * OR /");

    left = std::make_unique<BinaryOperationNode>(type, std::move(left),
                                                 std::move(right));
  }

  return left;
}

std::unique_ptr<ast> Parser::ParseExpression() {

  std::unique_ptr<ast> left = ParseTerm();
  while (Peek().type == TokenType::PLUS || Peek().type == TokenType::MINUS) {
    TokenType type = Peek().type;
    Consume();

    std::unique_ptr<ast> right = ParseTerm();

    if (!right)
      throw std::runtime_error("EXPECTED A NUMBER AFTER + OR -");

    left = std::make_unique<BinaryOperationNode>(type, std::move(left),
                                                 std::move(right));
  }

  return left;
}

std::unique_ptr<VariableDeclareNode> Parser::ParseVariable() {
  Expect(TokenType::LET);
  Token name = Expect(TokenType::IDENTIFIER);

  Expect(TokenType::COLON);
  Token type = Expect(TokenType::TYPES);
  unsigned size = 1;

  if (Peek().type == TokenType::LBRACKET) {
    Consume();
    Token somerandomval = Expect(TokenType::INT_LITERAL);
    Expect(RBRACKET);
    size += std::stoi(somerandomval.value) - 1;
  }

  std::unique_ptr<ast> val = nullptr;
  if (Peek().type == TokenType::EQ) {
    Consume();
    val = ParseExpression();

    if (auto arrNode = dynamic_cast<ArrayLiteralNode *>(val.get())) {
      if (arrNode->Elements.size() > size) {
        throw std::runtime_error("Array size mismatch: declared size " +
                                 std::to_string(size) + ", got " +
                                 std::to_string(arrNode->Elements.size()));
      }
    }
  }

  Expect(TokenType::SEMICOLON);

  return std::make_unique<VariableDeclareNode>(name.value, std::move(val), type,
                                               size);
}

std::unique_ptr<FunctionNode> Parser::ParseFunction() {
  Expect(TokenType::FUNC);
  Token name = Expect(TokenType::IDENTIFIER);
  Expect(LPAREN);
  std::vector<std::pair<std::string, llvm::Type *>> args;

  while (Peek().type != RPAREN) {
    Token paramName = Expect(IDENTIFIER);
    Expect(COLON);
    Token type = Expect(TYPES);
    llvm::Type *llvmType = GetTypeVoid(type, *cc.TheContext);

    unsigned arraySize = 0;
    if (Peek().type == LBRACKET) {
      Consume();
      Token sizeTok = Expect(INT_LITERAL);
      arraySize = std::stoi(sizeTok.value);
      Expect(RBRACKET);

      // Create LLVM array type
      llvmType = llvm::ArrayType::get(llvmType, arraySize);
    }

    args.push_back({paramName.value, llvmType});

    if (Peek().type == COMMA) {
      Consume();
    } else {
      break;
    }
  }
  Expect(RPAREN);
  Expect(DASHGREATER);
  auto rettype = Expect(TYPES);
  std::unique_ptr<ast> block = ParseStatement();
  if (!block)
    throw std::runtime_error("Function missing body");

  return std::make_unique<FunctionNode>(name.value, args, std::move(block),
                                        rettype);
}

std::unique_ptr<CompoundNode> Parser::ParseCompound() {
  std::vector<std::unique_ptr<ast>> vals;

  Expect(LBRACE);

  while (Peek().type != RBRACE) {
    if (Peek().type == SEMICOLON) {
      Consume();
      continue;
    }
    std::unique_ptr<ast> val = ParseStatement();
    if (!val) {
      throw std::runtime_error("ERROR: Invalid syntax " +
                               std::string(tokenName(Peek().type)));
    }
    vals.push_back(std::move(val));
    // Consume();
  }
  // std::cout << tokenName(Peek().type) << std::endl;
  Expect(RBRACE);

  return std::make_unique<CompoundNode>(std::move(vals));
}
std::unique_ptr<ReturnNode> Parser::ParseReturn() {
  Expect(RETURN);
  auto val = ParseExpression();
  if (!val)
    throw std::runtime_error("ERROR: Return statement MIssing Expression");
  // std::cout << tokenName(Peek().type) << std::endl;
  Expect(SEMICOLON);
  return std::make_unique<ReturnNode>(std::move(val));
}

std::unique_ptr<IfNode> Parser::ParseIfElse() {
  Expect(IF);
  // Expect(LPAREN);
  auto args = ParseExpression();
  if (!args) {
    throw std::runtime_error("If statement missing condition");
  }

  // Expect(RPAREN);
  std::unique_ptr<ast> TrueBlock = ParseStatement();

  if (!TrueBlock) {
    throw std::runtime_error("If statement missing TrueBlock");
  }

  std::unique_ptr<ast> ElseBlock = nullptr;

  if (Peek().type == ELSE) {
    Expect(ELSE);
    ElseBlock = ParseStatement();
    if (!ElseBlock) {
      throw std::runtime_error("If statement missing ElseBlock");
    }
  }

  return std::make_unique<IfNode>(std::move(args), std::move(TrueBlock),
                                  std::move(ElseBlock));
}

std::unique_ptr<WhileNode> Parser::ParseWhile() {
  Expect(WHILE);
  Expect(LPAREN);
  auto args = ParseExpression();
  Expect(RPAREN);
  auto block = ParseStatement();

  return std::make_unique<WhileNode>(std::move(args), std::move(block));
}

std::unique_ptr<ForNode> Parser::ParseFor() {
  Expect(FOR);
  Expect(LPAREN);

  auto init = ParseExpression();

  Expect(SEMICOLON);

  auto condition = ParseExpression();

  Expect(SEMICOLON);

  auto incremnt = ParseExpression();

  Expect(RPAREN);

  auto body = ParseStatement();

  return std::make_unique<ForNode>(std::move(init), std::move(condition),
                                   std::move(incremnt), std::move(body));
}

std::unique_ptr<ast> Parser::ParseAssignment() {
  Token name = Expect(IDENTIFIER);

  if (Peek().type == EQ) {
    Consume();
    auto val = ParseExpression();

    Expect(SEMICOLON);

    return std::make_unique<AssignmentNode>(name.value, std::move(val));
  } else if (Peek().type == LBRACKET) {
    Consume();
    auto locaiton = ParseExpression();
    Expect(RBRACKET);

    Expect(EQ);
    auto val = ParseExpression();
    Expect(SEMICOLON);

    return std::make_unique<ArrayAssignNode>(name.value, std::move(locaiton),
                                             std::move(val));
  }
  return nullptr;
}

std::unique_ptr<ast> Parser::ParseStatement() {
  if (Peek().type == TokenType::LET) {
    return ParseVariable();
  } else if (Peek().type == FUNC) {
    return ParseFunction();
  } else if (Peek().type == RETURN) {
    return ParseReturn();
  } else if (Peek().type == LBRACE) {
    return ParseCompound();
  } else if (Peek().type == IF) {
    return ParseIfElse();
  } else if (Peek().type == WHILE) {
    return ParseWhile();
  } else if (Peek().type == FOR) {
    return ParseFor();
  } else if (Peek().type == IDENTIFIER) {
    return ParseAssignment();
  } else {
    if (auto v = ParseExpression()) {
      return v;
    } else {
      if (Peek().type == SEMICOLON)
        return nullptr;

      throw std::runtime_error("UnExpected Token" +
                               std::string(tokenName(Peek().type)));
    }
  }
}

std::vector<std::unique_ptr<ast>> Parser::Parse() {
  std::vector<std::unique_ptr<ast>> output;

  while (Peek().type != TokenType::EOF_TOKEN) {
    // skip stray semicolons between top-level statements
    if (Peek().type == TokenType::SEMICOLON) {
      Consume();
      continue;
    }

    auto stmt = ParseStatement();
    if (!stmt) {
      throw std::runtime_error(std::string("Unexpected token in Parse(): ") +
                               tokenName(Peek().type));
    }

    output.push_back(std::move(stmt));
  }
  return output;
}

int main() {
  std::string src = R"(
  func to_upper(c:Char) -> Char {
	if c >= 97 && c <= 122 {
		return c - 32;
	} else {
	   return 0;
	}
  }

  func to_lower(c:Char) -> Char {
	  if c >= 65 && c <= 90 {
		 return c + 32;
	  } else { 
		 return 0;
	  }
  }

  func main() -> Integer {
	  let a:Integer = 1;
	  let b:Char[13] = "hello world";

	  let i:Integer = 0;

	  for (i = 0; i < sizeof(b); i = i + 1) {
		b[i] = to_upper(b[i]);
	  }

	  @Syscall("write",1, b, sizeof(b));
	  let x:Integer = 5;
	  let y:Integer* = &x;
	  let z:Integer = *y;

	  return 0;
  }


  )";

  Lexer lexer(src);
  auto program = lexer.lexer();

  // int stmtNo = 0;
  // for (const auto &stmt : program) {
  //   std::cout << "  " << std::setw(12) << tokenName(stmt.type) << " : '"
  //             << stmt.value << "'\n";
  // }

  std::cout << Colors::BOLD << Colors::RED
            << "---------------------------------------------------------------"
               "---------------------------------------------------------------"
               "---------------------------------------------------------------"
               "-----------------------"
            << Colors::RESET << std::endl;
  Parser parser(program, "MYMODULE");
  auto val = parser.Parse();
  for (auto &v : val) {
    std::cout << v->repr() << std::endl;
  }

  auto &cc = parser.getCodegenContext();
  for (auto &v : val) {
    try {
      v->codegen(cc);
    } catch (const std::exception &e) {
      std::cerr << "Codegen error: " << e.what() << std::endl;
    }
  }

  cc.Module->print(llvm::outs(), nullptr);
}
