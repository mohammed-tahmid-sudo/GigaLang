#pragma once
#include <string>
#include <vector>

enum TokenType {
  // Headers
  VERSION,
  AUTHOR,
  IMPORT,
  SYSCALL,
  // Keywords
  LET,
  FUNC,
  RETURN,
  IF,
  ELSE,
  FOR,
  IN,
  WHILE,
  CLASS,
  STRUCT,
  SIZEOF,
  TYPES,
  BREAK,
  CONTINUE,

  IDENTIFIER,

  // Literals
  INT_LITERAL,
  FLOAT_LITERAL,
  CHAR_LITERAL,
  STRING_LITERAL,
  BOOLEAN_LITERAL,

  // Operators
  PLUS,
  MINUS,
  STAR,
  SLASH,
  EQ,
  EQEQ,
  NOTEQ,
  LT,
  GT,
  LTE,
  GTE,
  AND,
  OR,
  DOT,
  // Punctuation
  LPAREN,
  RPAREN,
  LBRACE,
  RBRACE,
  LBRACKET,
  RBRACKET,
  COLON,
  COMMA,
  DASHGREATER,
  ANDPERCENT,
  RANGE,
  SEMICOLON,
  VARIDIC,
  // End of file
  EOF_TOKEN
};

const char *tokenName(TokenType t);

struct Token {
  TokenType type;
  std::string value;

  unsigned ptrdepth = 0;

  unsigned line = 0;
  unsigned col = 0;

  std::string file = "<input>";
};

class Lexer {
  std::string input;
  size_t index = 0;

  unsigned line = 1;                // ← add
  unsigned col = 1;                 // ← add
  std::string filename = "<input>"; // ← add

public:
  Lexer(std::string inp) : input(inp) {}
  char Peek() const;
  char PeekNext() const;
  char PeekNextNext() const;
  void Consume();
  void skipWhiwSpace();
  static std::string toLower(const std::string &s) {
    std::string r;
    r.reserve(s.size());
    for (char c : s)
      r.push_back(std::tolower(static_cast<unsigned char>(c)));
    return r;
  }

  std::vector<Token> lexer();
};
