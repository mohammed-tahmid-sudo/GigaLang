#include "ast.h"
#include "lexer.h"
#include <Diagnosis.h>
#include <cctype>
#include <colors.h>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <llvm-18/llvm/ADT/STLExtras.h>
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
#include <llvm-18/llvm/TargetParser/Host.h>
#include <llvm/IR/IRPrintingPasses.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <memory>
#include <parser.h>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

Token Parser::Peek() {
  if (x < input.size()) {
    return input[x];
  }
  return {TokenType::EOF_TOKEN, ""};
}

Token Parser::PeekNext() {
  if (x + 1 < input.size()) {
    return input[x + 1];
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

  diag.error(loc(), "expected '" + std::string(tokenName(tk)) + "', got '" +
                        tokenName(Peek().type) + "'");

  throw Diagnostics::FatalError("parse failure");

  // throw std::runtime_error("EXPECTED " + std::string(tokenName(tk)) + ", GOT
  // " +
  //                          tokenName(Peek().type));
}

SourceLoc Parser::loc() {
  Token t = Peek();
  return {t.file, t.line, t.col};
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

    if (Peek().type == TYPES) {
      Token type = Peek();
      Expect(TYPES);
      Expect(RPAREN);
      auto val = ParseExpression();

      return std::make_unique<CastNode>(std::move(val),
                                        GetTypeNonVoid(type, cc));
    }

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

    // std::cout << Colors::RED << "Comming here " << Peek().value <<
    // Colors::RESET
    //           << std::endl;

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

      // return std::make_unique<SizeOfNode>(std::move(val));

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
    // Token name = Expect(STRING_LITERAL);
    Token name = Expect(INT_LITERAL);
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

    return std::make_unique<SyscallNode>(std::stoi(name.value),
                                         std::move(args));
  } else if (Peek().type == ANDPERCENT) {
    Consume();
    Token name = Expect(IDENTIFIER);
    return std::make_unique<PointerReferenceNode>(name.value);

  } else if (Peek().type == STAR) {

    Consume();
    if (Peek().type == IDENTIFIER) {
      Token v = Peek();
      Consume();
      if (Peek().type == LBRACKET) {
        Consume();
        auto idx = ParseExpression();
        Expect(RBRACKET);

        if (Peek().type == EQ) {
          Consume();
          auto val = ParseExpression();
          return std::make_unique<PointerDeReferenceAssingNode>(
              v.value, std::move(val), std::move(idx));
        }
        return std::make_unique<DeReferenceNode>(v.value, std::move(idx));
      }

      return std::make_unique<DeReferenceNode>(v.value, nullptr);
    }
    return nullptr;

  } else {
    if (Peek().type == SEMICOLON) {
      return nullptr;
    } else {
      diag.error(loc(), "unexpected token '" +
                            std::string(tokenName(Peek().type)) +
                            "' in expression");
      return nullptr;
    }
  }
}

std::unique_ptr<ast> Parser::ParseTerm() {
  std::unique_ptr<ast> left = ParseFactor();
  while (Peek().type == TokenType::STAR || Peek().type == TokenType::SLASH) {
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

std::unique_ptr<ast> Parser::ParseAddSub() {
  std::unique_ptr<ast> left = ParseTerm();
  while (Peek().type == TokenType::PLUS || Peek().type == TokenType::MINUS) {
    TokenType type = Peek().type;
    Consume();
    std::unique_ptr<ast> right = ParseTerm();
    if (!right)
      throw std::runtime_error("Expected expression after + or -");
    left = std::make_unique<BinaryOperationNode>(type, std::move(left),
                                                 std::move(right));
  }
  return left;
}
std::unique_ptr<ast> Parser::ParseComparison() {
  std::unique_ptr<ast> left = ParseAddSub();
  while (Peek().type == TokenType::GT || Peek().type == TokenType::GTE ||
         Peek().type == TokenType::LT || Peek().type == TokenType::LTE ||
         Peek().type == TokenType::EQEQ || Peek().type == NOTEQ) {
    TokenType type = Peek().type;
    Consume();
    std::unique_ptr<ast> right = ParseAddSub();
    if (!right)
      throw std::runtime_error("Expected expression after comparison");
    left = std::make_unique<BinaryOperationNode>(type, std::move(left),
                                                 std::move(right));
  }
  return left;
}

std::unique_ptr<ast> Parser::ParseExpression() {
  std::unique_ptr<ast> left = ParseComparison();
  while (Peek().type == TokenType::AND) {
    TokenType type = Peek().type;
    Consume();
    std::unique_ptr<ast> right = ParseComparison();
    if (!right)
      throw std::runtime_error("Expected expression after &&");
    left = std::make_unique<BinaryOperationNode>(type, std::move(left),
                                                 std::move(right));
  }
  return left;
}

std::unique_ptr<VariableDeclareNode> Parser::ParseVariable() {
  Expect(TokenType::LET);
  Token name = Expect(TokenType::IDENTIFIER);

  Expect(TokenType::COLON);
  Token type;

  if (Peek().type == TokenType::TYPES) {
    type = Expect(TokenType::TYPES);
  } else if (Peek().type == TokenType::IDENTIFIER) {
    type = Expect(TokenType::IDENTIFIER);
  } else {
    throw std::runtime_error("Expected TYPES or IDENTIFIER");
  }

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
        diag.error(loc(),
                   "array initializer has " +
                       std::to_string(arrNode->Elements.size()) +
                       " elements but declared size is " + std::to_string(size),
                   "reduce initializer or increase declared size: let x:" +
                       type.value + "[" +
                       std::to_string(arrNode->Elements.size()) + "]");
      }
    }
  }

  Expect(TokenType::SEMICOLON);

  return std::make_unique<VariableDeclareNode>(name.value, std::move(val), type,
                                               size);
}

std::unique_ptr<FunctionNode> Parser::ParseFunction() {
  Expect(TokenType::FUNC);
  bool varidicType = false;
  Token name = Expect(TokenType::IDENTIFIER);
  Expect(LPAREN);
  std::vector<std::tuple<std::string, llvm::Type *>> args;

  while (Peek().type != RPAREN) {
    Token paramName = Expect(IDENTIFIER);
    Expect(COLON);
    Token type = Expect(TYPES);
    llvm::Type *llvmType = GetTypeVoid(type, cc);

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
      if (Peek().type == VARIDIC) {
        std::cout << "COMMING HErE" << std::endl;
        Consume();
        varidicType = true;
        break;
      }
    } else {
      break;
    }
  }
  Expect(RPAREN);
  Expect(DASHGREATER);
  auto rettype = Expect(TYPES);
  std::unique_ptr<ast> block = ParseStatement();
  if (!block)
    diag.error(loc(), "function '" + name.value + "' has no body");

  return std::make_unique<FunctionNode>(name.value, args, std::move(block),
                                        rettype, varidicType);
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
  // if (!val)
  //   throw std::runtime_error("ERROR: Return statement MIssing Expression");
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
  Token name;
  if (Peek().type == IDENTIFIER &&
      (PeekNext().type == EQ || PeekNext().type == LBRACKET)) {
    name = Expect(IDENTIFIER);
  }
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
    // Expect(SEMICOLON);

    return std::make_unique<ArrayAssignNode>(name.value, std::move(locaiton),
                                             std::move(val));
  }
  return nullptr;
}

std::unique_ptr<StructCreateNode> Parser::ParseStruct() {
  Expect(STRUCT);
  Token name = Expect(IDENTIFIER);
  Expect(LBRACKET);

  std::unordered_map<std::string, llvm::Type *> types;
  while (Peek().type != RBRACKET) {
    Token identifier = Expect(IDENTIFIER);
    Expect(COLON);
    Token type = Expect(TYPES);

    types.emplace(identifier.value, GetTypeNonVoid(type, cc));

    if (Peek().type == COMMA) {
      Expect(COMMA);
    } else {
      break;
    }
  }
  Expect(RBRACKET);
  Expect(SEMICOLON);
  return std::make_unique<StructCreateNode>(name.value, types);
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
  } else if (Peek().type == STRUCT) {
    return ParseStruct();
  } else if (Peek().type == BREAK) {
    Consume();
    return std::make_unique<BreakNode>();
  } else if (Peek().type == CONTINUE) {
    return std::make_unique<ContinueNode>();
    Consume();
  } else if (Peek().type == IDENTIFIER) {
    if (auto v = ParseAssignment()) {
      return v;
    } else {
      return ParseExpression();
    }
  } else {
    if (auto v = ParseExpression()) {
      // Expect(SEMICOLON);
      return v;
    } else {
      if (Peek().type == SEMICOLON)
        return nullptr;

      // throw std::runtime_error("UnExpected Token" +
      // std::string(tokenName(Peek().type)));
      throw Diagnostics::FatalError("Unexpected Token " +
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

// ===============================
// Utility Functions
// ===============================

// Print LLVM IR with line numbers
void printIRWithLineNumbers(llvm::Module *module) {
  std::string irStr;
  llvm::raw_string_ostream rso(irStr);
  module->print(rso, nullptr);
  rso.flush();

  std::istringstream iss(irStr);
  std::string line;
  int lineno = 1;
  while (std::getline(iss, line)) {
    std::cout << std::setw(4) << lineno << ": " << line << "\n";
    lineno++;
  }
}

// Save IR to file, compile to object, and link to executable
void saveIRAndCompile(llvm::Module *module, const std::string &filename) {
  // --- Save LLVM IR to a file ---
  std::error_code EC;
  llvm::raw_fd_ostream dest(filename + ".ll", EC, llvm::sys::fs::OF_None);
  if (EC) {
    std::cerr << "Could not open file: " << EC.message() << std::endl;
    return;
  }
  module->print(dest, nullptr);
  dest.close();

  // --- Compile IR to object file using llc ---
  std::string objFile = filename + ".o";
  std::string llcCmd = "llc " + filename + ".ll -filetype=obj -o " + objFile;
  if (system(llcCmd.c_str()) != 0) {
    std::cerr << "Error running llc" << std::endl;
    return;
  }

  // --- Link object file to create executable using clang ---
  std::string exeFile = filename + "_exec";
  std::string clangCmd = "clang " + objFile + " -o " + exeFile;
  if (system(clangCmd.c_str()) != 0) {
    std::cerr << "Error running clang" << std::endl;
    return;
  }

  std::cout << "Executable created: " << exeFile << std::endl;
}

// ===============================
// Main Function
// ===============================

int main() {
  // --- Source Code to Compile ---
  std::string src = R"(

func to_lower(c:Char*) -> Void {
    let i:Integer = 0;
    while (*c[i] != '\0') {
        if *c[i] >= 'A' && *c[i] <= 'Z' {
            *c[i] = (Char)(*c[i] + ('a' - 'A'));
        }
        i = i + 1;
    }
}

func string_concat(a:Char*, b:Char*) -> Void {
    let i:Integer = 0;
    while (*a[i] != 0) { i = i + 1; }  // find end of a
    let j:Integer = 0;
    while (*b[j] != 0) {               // walk b
        *a[i + j] = *b[j];            // append b onto end of a
        j = j + 1;
    }
    *a[i + j] = 0;
}

func itoa(n:Integer, str:Char*) -> Integer  {
    let i:Integer = 0;
    let isNegetive:Boolean = false;

    if n == 0 {
        *str[i] = '0';        ///////////////////////
        i = i + 1;
        *str[i] = '\0';       ///////////////////////
        return i;
    }

    if n < 0 {
        isNegetive = true;
        n = n - n * 2;
    }

    while (n != 0) {
        *str[i] = (Char)(n - (n / 10) * 10 + 48);   ///////////////////////
        i = i + 1;
        n = n / 10;
    }

    if isNegetive {
        *str[i] = '-';        ///////////////////////
        i = i + 1;
    }

    *str[i] = '\0';           ///////////////////////

    let j:Integer = 0;
    let k:Integer = i - 1;

    while (j < k) {
        let tmp:Char = *str[j] ;   ///////////////////////
        *str[j] = *str[k];     ///////////////////////
        *str[k] = tmp;            ///////////////////////

        j = j + 1;
        k = k - 1;
    }

  return i;
}


func printf(str: Char*, vals: Char*) -> Void {
	let i:Integer = 0;
	let j:Integer = 0;
	while (*str[i] != '\0') {
		if *str[i] == '%' {
			i = i + 1;

			if *str[i] == 's' {
				let cptr:Char = *vals[j];
				@Syscall(1, 1, &cptr, 1, 0, 0);
				j = j + 1;
				i = i + 1;
			} else if *str[i] =='d' {
				let val:Integer = (Integer)*vals[j];
				let cptr:Char[32];

				let len:Integer = itoa(val, &cptr);

				@Syscall(1, 1, &cptr, len, 0, 0);
				j = j + 1;
				i = i + 1;
			}
		}
		let val:Char = *str[i];
		@Syscall(1, 1, &val , 1, 0, 0);

		i = i + 1;
	}
}

func to_upper(c:Char*) -> Void {
    let i:Integer = 0;
    while (*c[i] != '\0') {
        if *c[i] >= 'a' && *c[i] <= 'z' {
            *c[i] = (Char)(*c[i] - ('a' - 'A'));
        }
		if *c[i] == '0' {
			break;
		}
        i = i + 1;
    }

}

struct name [
	b:Char, 
	k:Integer
]; 



func main() -> Integer {
    let name:Char[25];
	let num:Integer = 42;

	let something:name = ['a'];
    
    let args:Char[2] = [(Char)(num)];
    

    // printf("Hello %s,  ", (Char*)&args);
	// itoa(588, &name)
	printf("Hello %d", &args);  
		// @Syscall(1, 1, &name , sizeof(name), 0, 0);
	
    return 0;
}
)";

  std::vector<std::string> sourceLines;
  {
    std::istringstream ss(src);
    std::string line;
    while (std::getline(ss, line))
      sourceLines.push_back(line);
  }

  Diagnostics diag(sourceLines);

  // --- Lexical Analysis ---
  Lexer lexer(src);
  auto program = lexer.lexer();

  std::cout << "Tokens:\n";
  int count = 0;
  for (const auto &stmt : program) {
    std::cout << tokenName(stmt.type) << ":'" << stmt.value << "'  ";
    count++;
    if (count % 5 == 0) // 5 tokens per line
      std::cout << "\n";
  }
  if (count % 5 != 0)
    std::cout << "\n"; // print final newline if needed

  std::cout << Colors::BOLD << Colors::RED
            << "\n-------------------------------PARSED-AST--------------------"
               "------------------\n"
            << Colors::RESET << std::endl;

  // --- Parsing ---
  Parser parser(program, "MYMODULE", diag);
  auto astNodes = parser.Parse();

  // std::cout << "AST Nodes:\n";
  // for (auto &v : astNodes) {
  //   std::cout << v->repr() << std::endl;
  // }

  // --- Code Generation ---
  auto &cc = parser.getCodegenContext();
  for (auto &v : astNodes) {
    try {
      v->codegen(cc);
    } catch (const std::exception &e) {
      std::cerr << "Codegen error: " << e.what() << std::endl;
    }
  }

  std::cout << Colors::BOLD << Colors::RED
            << "\n-------------------------------LLVM_IR-----------------------"
               "---------------\n"
            << Colors::RESET << std::endl;
  printIRWithLineNumbers(cc.Module.get());

  std::cout << Colors::BOLD << Colors::RED
            << "\n-------------------------------COMPILED_OUTPUT---------------"
               "-----------------------\n"
            << Colors::RESET << std::endl;

  saveIRAndCompile(cc.Module.get(), "output");

  return 0;
}
