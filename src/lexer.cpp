#include <ast.h>
#include <cctype>
#include <colors.h>
#include <cstdio>
#include <iomanip>
#include <lexer.h>
#include <string>
#include <vector>

char Lexer::Peek() const { return index < input.size() ? input[index] : 0; }

char Lexer::PeekNext() const {
  return (index + 1) < input.size() ? input[index + 1] : 0;
}

char Lexer::PeekNextNext() const {
  return (index + 2) < input.size() ? input[index + 2] : 0;
}

void Lexer::Consume() {
  if (index < input.size()) {
    if (input[index] == '\n') { // ← newline resets col, bumps line
      line++;
      col = 1;
    } else {
      col++;
    }
    index++;
  }
}

void Lexer::skipWhiwSpace() {
  while (std::isspace(Peek()))
    Consume();
}

std::vector<Token> Lexer::lexer() {
  std::vector<Token> out;

  while (true) {
    skipWhiwSpace(); // advances line/col through whitespace correctly now
    char c = Peek();
    if (c == 0)
      break;

    // ── snapshot BEFORE consuming anything ──
    unsigned tokLine = line;
    unsigned tokCol = col;

    // Comments (no token emitted, just skip)
    if (c == '/' && PeekNext() == '/') {
      while (Peek() != '\n' && Peek() != 0)
        Consume();
      continue;
    }
    if (c == '/' && PeekNext() == '*') {
      Consume();
      Consume();
      while (!(Peek() == '*' && PeekNext() == '/') && Peek() != 0)
        Consume();
      if (Peek() == '*') {
        Consume();
        if (Peek() == '/')
          Consume();
      }
      continue;
    }

    // helper lambda — stamps line/col onto token
    auto make = [&](TokenType type, std::string value) -> Token {
      return Token{type, std::move(value), tokLine, tokCol, filename};
    };

    // @directives
    if (c == '@') {
      Consume();
      std::string word;
      while (std::isalpha(Peek())) {
        word.push_back(Peek());
        Consume();
      }
      std::string lw = toLower(word);
      TokenType type = IDENTIFIER;
      if (lw == "version")
        type = VERSION;
      else if (lw == "author")
        type = AUTHOR;
      else if (lw == "import")
        type = IMPORT;
      else if (lw == "syscall")
        type = SYSCALL;
      out.push_back(make(type, word));
      continue;
    }

    // String literal
    if (c == '"') {
      Consume();
      std::string val;
      while (Peek() != '"' && Peek() != 0) {
        if (Peek() == '\\') {
          Consume();
          char esc = Peek();
          switch (esc) {
          case 'n':
            val += '\n';
            break;
          case 't':
            val += '\t';
            break;
          case '"':
            val += '"';
            break;
          case '\\':
            val += '\\';
            break;
          default:
            val += esc;
            break;
          }
          Consume();
        } else {
          val += Peek();
          Consume();
        }
      }
      if (Peek() == '"')
        Consume();
      out.push_back(make(STRING_LITERAL, val));
      continue;
    }

    // Char literal
    if (c == '\'') {
      Consume();
      char value = 0;
      if (Peek() == '\\') {
        Consume();
        char esc = Peek();
        switch (esc) {
        case 'n':
          value = '\n';
          break;
        case 't':
          value = '\t';
          break;
        case '0':
          value = '\0';
          break;
        case '\'':
          value = '\'';
          break;
        case '\\':
          value = '\\';
          break;
        default:
          value = esc;
          break;
        }
        Consume();
      } else {
        value = Peek();
        Consume();
      }
      if (Peek() == '\'')
        Consume();
      out.push_back(make(CHAR_LITERAL, std::string(1, value)));
      continue;
    }

    // Numbers
    if (std::isdigit(c)) {
      std::string num;
      while (std::isdigit(Peek())) {
        num += Peek();
        Consume();
      }
      if (Peek() == '.' && std::isdigit(PeekNext())) {
        num += Peek();
        Consume();
        while (std::isdigit(Peek())) {
          num += Peek();
          Consume();
        }
        out.push_back(make(FLOAT_LITERAL, num));
      } else {
        out.push_back(make(INT_LITERAL, num));
      }
      continue;
    }

    // Identifiers / keywords / types
    if (std::isalpha(c) || c == '_') {
      std::string id;
      while (std::isalnum(Peek()) || Peek() == '_') {
        id += Peek();
        Consume();
      }
      std::string lower = toLower(id);

      if (lower == "true" || lower == "false") {
        out.push_back(make(BOOLEAN_LITERAL, id));
        continue;
      }
      if (lower == "let") {
        out.push_back(make(LET, id));
        continue;
      }
      if (lower == "func") {
        out.push_back(make(FUNC, id));
        continue;
      }
      if (lower == "return") {
        out.push_back(make(RETURN, id));
        continue;
      }
      if (lower == "if") {
        out.push_back(make(IF, id));
        continue;
      }
      if (lower == "else") {
        out.push_back(make(ELSE, id));
        continue;
      }
      if (lower == "for") {
        out.push_back(make(FOR, id));
        continue;
      }
      if (lower == "in") {
        out.push_back(make(IN, id));
        continue;
      }
      if (lower == "while") {
        out.push_back(make(WHILE, id));
        continue;
      }
      if (lower == "struct") {
        out.push_back(make(STRUCT, id));
        continue;
      }
      if (lower == "class") {
        out.push_back(make(CLASS, id));
        continue;
      }
      if (lower == "sizeof") {
        out.push_back(make(SIZEOF, id));
        continue;
      }
      if (lower == "break") {
        out.push_back(make(BREAK, id));
        continue;
      }
      if (lower == "continue") {
        out.push_back(make(CONTINUE, id));
        continue;
      }

      if (id == "Integer" || id == "Float" || id == "Boolean" ||
          id == "String" || id == "Void" || id == "Char") {
        std::string typeName = id;
        if (Peek() == '*') {
          Consume();
          typeName += "POINTER";
        }
        out.push_back(make(TYPES, typeName));
        continue;
      }

      out.push_back(make(IDENTIFIER, id));
      continue;
    }

    // Two-char operators
    if (c == '=' && PeekNext() == '=') {
      Consume();
      Consume();
      out.push_back(make(EQEQ, "=="));
      continue;
    }
    if (c == '!' && PeekNext() == '=') {
      Consume();
      Consume();
      out.push_back(make(NOTEQ, "!="));
      continue;
    }
    if (c == '<' && PeekNext() == '=') {
      Consume();
      Consume();
      out.push_back(make(LTE, "<="));
      continue;
    }
    if (c == '>' && PeekNext() == '=') {
      Consume();
      Consume();
      out.push_back(make(GTE, ">="));
      continue;
    }
    if (c == '&' && PeekNext() == '&') {
      Consume();
      Consume();
      out.push_back(make(AND, "&&"));
      continue;
    }
    if (c == '|' && PeekNext() == '|') {
      Consume();
      Consume();
      out.push_back(make(OR, "||"));
      continue;
    }
    if (c == '-' && PeekNext() == '>') {
      Consume();
      Consume();
      out.push_back(make(DASHGREATER, "->"));
      continue;
    }
    if (c == '.' && PeekNext() == '.' && PeekNextNext() == '.') {
      Consume();
      Consume();
      Consume();
      out.push_back(make(VARIDIC, "..."));
      continue;
    }
    if (c == '.' && PeekNext() == '.') {
      Consume();
      Consume();
      out.push_back(make(RANGE, ".."));
      continue;
    }

    // Single-char operators
    switch (c) {
    case '+':
      Consume();
      out.push_back(make(PLUS, "+"));
      break;
    case '-':
      Consume();
      out.push_back(make(MINUS, "-"));
      break;
    case '*':
      Consume();
      out.push_back(make(STAR, "*"));
      break;
    case '/':
      Consume();
      out.push_back(make(SLASH, "/"));
      break;
    case '=':
      Consume();
      out.push_back(make(EQ, "="));
      break;
    case '<':
      Consume();
      out.push_back(make(LT, "<"));
      break;
    case '>':
      Consume();
      out.push_back(make(GT, ">"));
      break;
    case '(':
      Consume();
      out.push_back(make(LPAREN, "("));
      break;
    case ')':
      Consume();
      out.push_back(make(RPAREN, ")"));
      break;
    case '{':
      Consume();
      out.push_back(make(LBRACE, "{"));
      break;
    case '}':
      Consume();
      out.push_back(make(RBRACE, "}"));
      break;
    case '[':
      Consume();
      out.push_back(make(LBRACKET, "["));
      break;
    case ']':
      Consume();
      out.push_back(make(RBRACKET, "]"));
      break;
    case ':':
      Consume();
      out.push_back(make(COLON, ":"));
      break;
    case ',':
      Consume();
      out.push_back(make(COMMA, ","));
      break;
    case ';':
      Consume();
      out.push_back(make(SEMICOLON, ";"));
      break;
    case '&':
      Consume();
      out.push_back(make(ANDPERCENT, "&"));
      break;
    default:
      Consume();
      out.push_back(make(IDENTIFIER, std::string(1, c)));
      break;
    }
  }

  out.push_back({EOF_TOKEN, "", line, col, filename});
  return out;
}

const char *tokenName(TokenType t) {
  switch (t) {
  case VERSION:
    return "VERSION";
  case AUTHOR:
    return "AUTHOR";
  case IMPORT:
    return "IMPORT";
  case SYSCALL:
    return "SYSCALL";
  case LET:
    return "LET";
  case FUNC:
    return "FUNC";
  case RETURN:
    return "RETURN";
  case IF:
    return "IF";
  case ELSE:
    return "ELSE";
  case FOR:
    return "FOR";
  case IN:
    return "IN";
  case WHILE:
    return "WHILE";
  case CLASS:
    return "CLASS";
  case BOOLEAN_LITERAL:
    return "BOOLEAN_LITERAL";
  case IDENTIFIER:
    return "IDENTIFIER";
  case INT_LITERAL:
    return "INT_LITERAL";
  case FLOAT_LITERAL:
    return "FLOAT_LITERAL";
  case PLUS:
    return "PLUS";
  case MINUS:
    return "MINUS";
  case STAR:
    return "STAR";
  case SLASH:
    return "SLASH";
  case EQ:
    return "EQ";
  case EQEQ:
    return "EQEQ";
  case NOTEQ:
    return "NOTEQ";
  case LT:
    return "LT";
  case GT:
    return "GT";
  case LTE:
    return "LTE";
  case GTE:
    return "GTE";
  case AND:
    return "AND";
  case OR:
    return "OR";
  case LPAREN:
    return "LPAREN";
  case RPAREN:
    return "RPAREN";
  case LBRACE:
    return "LBRACE";
  case RBRACE:
    return "RBRACE";
  case COLON:
    return "COLON";
  case COMMA:
    return "COMMA";
  case DASHGREATER:
    return "DASHGREATER";
  case RANGE:
    return "RANGE";
  case EOF_TOKEN:
    return "EOF";
  case TYPES:
    return "Types";
  case SEMICOLON:
    return "SEMICOLON";
  case LBRACKET:
    return "LBRACKET";
  case RBRACKET:
    return "RBRACKET";
  case STRING_LITERAL:
    return "STRING_LITERAL";
  case SIZEOF:
    return "SIZEOF";
  case ANDPERCENT:
    return "ANDPERCENT";
  case VARIDIC:
    return "VARIDIC";
  case CHAR_LITERAL:
    return "CHAR_LITERAL";
  case CONTINUE:
    return "CONTINUE";
  case BREAK:
    return "BREAK";
  case STRUCT:
	return "STRUCT";
  default:
    return "UNKNOWN";
  }
}

// int main() {
//   std::string src = R"(
//   @version "1.0";
//   @author "Tahmid";

//   let x: Integer = 10;
//   let something:Integer* = &x;
//   let y: Float = 3.14;

//   let y: Integer[2] = [21, 12];
//   ley something: Char{32} = {'s', 'b', 'c', 'd'. 'e' , '\0'};

//   func add(a: Integer, b: Integer) -> void {
// 	return a + b;
//   }

//   if x >= 5 {
// 	2 + 1;
//   } else {
// 	2 + 1;
//   }

//   while (True) {
// 	break;
//   }

//   for i in 0..10 {
// 	2 + 1;
//   }

//   struct RandomStruct {
// 	let a:Integer;
//   };

//   )";

//   // std::string src = R"(
//   // let x:Integer;
//   // )";
//   Lexer lexer(src);
//   auto program = lexer.lexer();

//   int stmtNo = 0;
//   for (const auto &stmt : program) {
//     std::cout << "  " << std::setw(12) << tokenName(stmt.type) << " : '"
//               << stmt.value << "'\n";
//   }
// }

// ─────────────────────────────────────────────
// // Minimal test framework
// // ─────────────────────────────────────────────
// static int passed = 0;
// static int failed = 0;

// void check(bool cond, const std::string &desc) {
//   if (cond) {
//     std::cout << "  \033[1;32m✓\033[0m " << desc << "\n";
//     passed++;
//   } else {
//     std::cout << "  \033[1;31m✗\033[0m " << desc << "\n";
//     failed++;
//   }
// }

// void section(const std::string &name) {
//   std::cout << "\n\033[1;34m── " << name << " ──\033[0m\n";
// }

// // Helper: find the Nth token (0-based) with a given type in the token list
// const Token *findNth(const std::vector<Token> &toks, TokenType t, int n = 0)
// {
//   int count = 0;
//   for (auto &tok : toks)
//     if (tok.type == t && count++ == n)
//       return &tok;
//   return nullptr;
// }

// // ─────────────────────────────────────────────
// // Tests
// // ─────────────────────────────────────────────

// void test_basic_types() {
//   section("Basic type tokens");
//   Lexer lex("Integer Float Boolean Char Void");
//   auto toks = lex.lexer();

//   check(toks[0].type == TYPES, "Integer is TYPES");
//   check(toks[0].value == "Integer", "Integer value");
//   check(toks[1].type == TYPES, "Float is TYPES");
//   check(toks[2].type == TYPES, "Boolean is TYPES");
//   check(toks[3].type == TYPES, "Char is TYPES");
//   check(toks[4].type == TYPES, "Void is TYPES");
// }

// void test_pointer_types() {
//   section("Pointer type tokens");
//   Lexer lex("Integer* Char* Float*");
//   auto toks = lex.lexer();

//   check(toks[0].type == TYPES, "Integer* is TYPES");
//   check(toks[0].value == "IntegerPOINTER", "Integer* value is
//   IntegerPOINTER"); check(toks[1].type == TYPES, "Char* is TYPES");
//   check(toks[1].value == "CharPOINTER", "Char* value is CharPOINTER");
// }

// void test_keywords() {
//   section("Keyword tokens");
//   Lexer lex("let func return if else for in while class sizeof break continue
//   "); auto toks = lex.lexer();

//   check(toks[0].type == LET, "let");
//   check(toks[1].type == FUNC, "func");
//   check(toks[2].type == RETURN, "return");
//   check(toks[3].type == IF, "if");
//   check(toks[4].type == ELSE, "else");
//   check(toks[5].type == FOR, "for");
//   check(toks[6].type == IN, "in");
//   check(toks[7].type == WHILE, "while");
//   check(toks[8].type == CLASS, "class");
//   check(toks[9].type == SIZEOF, "sizeof");
//   check(toks[10].type == BREAK, "break");
//   check(toks[11].type == CONTINUE, "continue");
// }

// void test_directives() {
//   section("@ directives");
//   Lexer lex("@version @author @import @syscall");
//   auto toks = lex.lexer();

//   check(toks[0].type == VERSION, "@version");
//   check(toks[1].type == AUTHOR, "@author");
//   check(toks[2].type == IMPORT, "@import");
//   check(toks[3].type == SYSCALL, "@syscall");
// }

// void test_boolean_literals() {
//   section("Boolean literals (case-insensitive)");
//   Lexer lex("true false TRUE FALSE True False");
//   auto toks = lex.lexer();

//   for (int i = 0; i < 6; i++)
//     check(toks[i].type == BOOLEAN_LITERAL,
//           "'" + toks[i].value + "' is BOOLEAN_LITERAL");
// }

// void test_integer_literals() {
//   section("Integer literals");
//   Lexer lex("0 42 100 9999");
//   auto toks = lex.lexer();

//   check(toks[0].type == INT_LITERAL && toks[0].value == "0", "0");
//   check(toks[1].type == INT_LITERAL && toks[1].value == "42", "42");
//   check(toks[2].type == INT_LITERAL && toks[2].value == "100", "100");
//   check(toks[3].type == INT_LITERAL && toks[3].value == "9999", "9999");
// }

// void test_float_literals() {
//   section("Float literals");
//   Lexer lex("3.14 0.5 100.001");
//   auto toks = lex.lexer();

//   check(toks[0].type == FLOAT_LITERAL && toks[0].value == "3.14", "3.14");
//   check(toks[1].type == FLOAT_LITERAL && toks[1].value == "0.5", "0.5");
//   check(toks[2].type == FLOAT_LITERAL && toks[2].value == "100.001",
//   "100.001");
// }

// void test_string_literals() {
//   section("String literals");
//   Lexer lex(R"("hello" "world" "with spaces")");
//   auto toks = lex.lexer();

//   check(toks[0].type == STRING_LITERAL && toks[0].value == "hello",
//         "\"hello\"");
//   check(toks[1].type == STRING_LITERAL && toks[1].value == "world",
//         "\"world\"");
//   check(toks[2].type == STRING_LITERAL && toks[2].value == "with spaces",
//         "\"with spaces\"");
// }

// void test_string_escape_sequences() {
//   section("String escape sequences");
//   Lexer lex(R"("line\nbreak" "tab\there" "quote\"here")");
//   auto toks = lex.lexer();

//   check(toks[0].value == "line\nbreak", "\\n escape");
//   check(toks[1].value == "tab\there", "\\t escape");
//   check(toks[2].value == "quote\"here", "\\\" escape");
// }

// void test_char_literals() {
//   section("Char literals");
//   Lexer lex("'a' 'z' '0' ' '");
//   auto toks = lex.lexer();

//   check(toks[0].type == CHAR_LITERAL && toks[0].value == "a", "'a'");
//   check(toks[1].type == CHAR_LITERAL && toks[1].value == "z", "'z'");
//   check(toks[2].type == CHAR_LITERAL && toks[2].value == "0", "'0'");
//   check(toks[3].type == CHAR_LITERAL && toks[3].value == " ", "' '");
// }

// void test_char_escape_sequences() {
//   section("Char escape sequences");
//   Lexer lex(R"('\n' '\t' '\0' '\\' '\'')");
//   auto toks = lex.lexer();

//   check(toks[0].type == CHAR_LITERAL && toks[0].value[0] == '\n', "'\\n'");
//   check(toks[1].type == CHAR_LITERAL && toks[1].value[0] == '\t', "'\\t'");
//   check(toks[2].type == CHAR_LITERAL && toks[2].value[0] == '\0', "'\\0'");
//   check(toks[3].type == CHAR_LITERAL && toks[3].value[0] == '\\', "'\\\\'");
//   check(toks[4].type == CHAR_LITERAL && toks[4].value[0] == '\'', "'\\''");
// }

// void test_operators() {
//   section("Operators");
//   Lexer lex("+ - * / = == != < > <= >= && ||");
//   auto toks = lex.lexer();

//   check(toks[0].type == PLUS, "+");
//   check(toks[1].type == MINUS, "-");
//   check(toks[2].type == STAR, "*");
//   check(toks[3].type == SLASH, "/");
//   check(toks[4].type == EQ, "=");
//   check(toks[5].type == EQEQ, "==");
//   check(toks[6].type == NOTEQ, "!=");
//   check(toks[7].type == LT, "<");
//   check(toks[8].type == GT, ">");
//   check(toks[9].type == LTE, "<=");
//   check(toks[10].type == GTE, ">=");
//   check(toks[11].type == AND, "&&");
//   check(toks[12].type == OR, "||");
// }

// void test_punctuation() {
//   section("Punctuation");
//   Lexer lex("( ) { } [ ] : , ; -> & .. ...");
//   auto toks = lex.lexer();

//   check(toks[0].type == LPAREN, "(");
//   check(toks[1].type == RPAREN, ")");
//   check(toks[2].type == LBRACE, "{");
//   check(toks[3].type == RBRACE, "}");
//   check(toks[4].type == LBRACKET, "[");
//   check(toks[5].type == RBRACKET, "]");
//   check(toks[6].type == COLON, ":");
//   check(toks[7].type == COMMA, ",");
//   check(toks[8].type == SEMICOLON, ";");
//   check(toks[9].type == DASHGREATER, "->");
//   check(toks[10].type == ANDPERCENT, "&");
//   check(toks[11].type == RANGE, "..");
//   check(toks[12].type == VARIDIC, "...");
// }

// void test_line_numbers() {
//   section("Line number tracking");
//   Lexer lex("let\nx\n=\n10\n;");
//   auto toks = lex.lexer();

//   check(toks[0].line == 1, "let  → line 1");
//   check(toks[1].line == 2, "x    → line 2");
//   check(toks[2].line == 3, "=    → line 3");
//   check(toks[3].line == 4, "10   → line 4");
//   check(toks[4].line == 5, ";    → line 5");
// }

// void test_column_numbers() {
//   section("Column number tracking");
//   //        col: 1234567890
//   Lexer lex("let x = 10;");
//   auto toks = lex.lexer();

//   check(toks[0].col == 1, "let → col 1");
//   check(toks[1].col == 5, "x   → col 5");
//   check(toks[2].col == 7, "=   → col 7");
//   check(toks[3].col == 9, "10  → col 9");
//   check(toks[4].col == 11, "10  → col 11");
// }

// void test_line_comments() {
//   section("Line comments are skipped");
//   Lexer lex("let x = 10; // this is a comment\nlet y = 20;");
//   auto toks = lex.lexer();

//   // Should only see tokens from the two let statements, no comment tokens
//   int identCount = 0;
//   for (auto &t : toks)
//     if (t.type == IDENTIFIER)
//       identCount++;
//   check(identCount == 2, "exactly 2 identifiers (x and y), comment skipped");
//   check(toks[5].type == LET && toks[5].line == 2, "second let on line 2");
// }

// void test_block_comments() {
//   section("Block comments are skipped");
//   Lexer lex("let /* this is\na block comment */ x = 5;");
//   auto toks = lex.lexer();

//   check(toks[0].type == LET, "let token");
//   check(toks[1].type == IDENTIFIER, "x token after block comment");
//   check(toks[1].value == "x", "x value correct");
// }

// void test_let_declaration() {
//   section("Full let declaration");
//   Lexer lex("let x: Integer = 42;");
//   auto toks = lex.lexer();
//   //          0    1  2       3  4  5

//   check(toks[0].type == LET, "let");
//   check(toks[1].type == IDENTIFIER, "x");
//   check(toks[1].value == "x", "x value");
//   check(toks[2].type == COLON, ":");
//   check(toks[3].type == TYPES, "Integer");
//   check(toks[4].type == EQ, "=");
//   check(toks[5].type == INT_LITERAL, "42");
//   check(toks[5].value == "42", "42 value");
//   check(toks[6].type == SEMICOLON, ";");
// }

// void test_func_declaration() {
//   section("Function declaration");
//   Lexer lex("func add(a: Integer, b: Integer) -> Integer { return a; }");
//   auto toks = lex.lexer();

//   check(toks[0].type == FUNC, "func");
//   check(toks[1].type == IDENTIFIER, "add");
//   check(toks[2].type == LPAREN, "(");
//   check(toks[3].type == IDENTIFIER, "a");
//   check(toks[4].type == COLON, ":");
//   check(toks[5].type == TYPES, "Integer");
//   check(toks[6].type == COMMA, ",");
//   check(toks[7].type == IDENTIFIER, "b");
//   check(toks[10].type == RPAREN, ")");
//   check(toks[11].type == DASHGREATER, "->");
//   check(toks[12].type == TYPES, "Integer return type");
// }

// void test_eof_token() {
//   section("EOF token");
//   Lexer lex("x");
//   auto toks = lex.lexer();

//   check(toks.back().type == EOF_TOKEN, "last token is EOF");
// }

// void test_identifier_vs_keyword() {
//   section("Identifiers vs keywords");
//   Lexer lex("letter forloop iffy");
//   auto toks = lex.lexer();

//   check(toks[0].type == IDENTIFIER && toks[0].value == "letter",
//         "'letter' is IDENTIFIER not LET");
//   check(toks[1].type == IDENTIFIER && toks[1].value == "forloop",
//         "'forloop' is IDENTIFIER not FOR");
//   check(toks[2].type == IDENTIFIER && toks[2].value == "iffy",
//         "'iffy' is IDENTIFIER not IF");
// }

// void test_range_and_variadic() {
//   section("Range and variadic");
//   Lexer lex("0..10 ...");
//   auto toks = lex.lexer();

//   check(toks[0].type == INT_LITERAL, "0");
//   check(toks[1].type == RANGE, "..");
//   check(toks[2].type == INT_LITERAL, "10");
//   check(toks[3].type == VARIDIC, "...");
// }

// // ─────────────────────────────────────────────
// // Main
// // ─────────────────────────────────────────────
// int main() {
//   std::cout << "\033[1;37mLexer Test Suite\033[0m\n";
//   std::cout << std::string(40, '=') << "\n";

//   test_basic_types();
//   test_pointer_types();
//   test_keywords();
//   test_directives();
//   test_boolean_literals();
//   test_integer_literals();
//   test_float_literals();
//   test_string_literals();
//   test_string_escape_sequences();
//   test_char_literals();
//   test_char_escape_sequences();
//   test_operators();
//   test_punctuation();
//   test_line_numbers();
//   test_column_numbers();
//   test_line_comments();
//   test_block_comments();
//   test_let_declaration();
//   test_func_declaration();
//   test_eof_token();
//   test_identifier_vs_keyword();
//   test_range_and_variadic();

//   std::cout << "\n" << std::string(40, '=') << "\n";
//   std::cout << "\033[1;32mPassed: " << passed << "\033[0m  ";
//   std::cout << "\033[1;31mFailed: " << failed << "\033[0m\n";

//   return failed > 0 ? 1 : 0;
// }
