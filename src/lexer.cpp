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
        while (Peek() == '*') {
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
//   let something:Integer**** = &x;
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
