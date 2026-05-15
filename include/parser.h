#include <Diagnosis.h>
#include <ast.h>
#include <lexer.h>
#include <memory>
#include <vector>
class Parser {
  std::vector<Token> input;
  size_t x = 0;
  CodegenContext cc;
  Diagnostics &diag;

public:
  Parser(std::vector<Token> inp, const std::string &s, Diagnostics &diag)
      : input(inp), cc(s), diag(diag) {}
  CodegenContext &getCodegenContext() { return cc; }

  // helper
  SourceLoc loc();

  Token Peek();
  Token PeekNext();
  Token Consume();
  Token Expect(TokenType tk);

  std::unique_ptr<ast> ParseFactor();
  std::unique_ptr<ast> ParsePointerFileld();
  std::unique_ptr<ast> ParseFileld();
  std::unique_ptr<ast> ParseAddSub();
  std::unique_ptr<ast> ParseComparison();
  std::unique_ptr<ast> ParseTerm();
  std::unique_ptr<ast> ParseAssignment();

  std::unique_ptr<ast> ParseExpression();

  std::unique_ptr<VariableDeclareNode> ParseVariable();
  std::unique_ptr<FunctionNode> ParseFunction();
  std::unique_ptr<CompoundNode> ParseCompound();
  std::unique_ptr<ReturnNode> ParseReturn();
  std::unique_ptr<IfNode> ParseIfElse();
  std::unique_ptr<WhileNode> ParseWhile();
  std::unique_ptr<ForNode> ParseFor();
  std::unique_ptr<StructCreateNode> ParseStruct();

  std::unique_ptr<ast> ParseStatement();

  std::vector<std::unique_ptr<ast>> Parse();
};
