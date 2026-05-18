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
    auto make = [&](TokenType type, std::string value,
                    unsigned ptrdepth) -> Token {
      return Token{type, std::move(value), ptrdepth, tokLine, tokCol, filename};
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
      out.push_back(make(type, word, 0));
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
      out.push_back(make(STRING_LITERAL, val, 0));
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
      out.push_back(make(CHAR_LITERAL, std::string(1, value), 0));
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
        out.push_back(make(FLOAT_LITERAL, num, 0));
      } else {
        out.push_back(make(INT_LITERAL, num, 0));
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
        out.push_back(make(BOOLEAN_LITERAL, id, 0));
        continue;
      }
      if (lower == "let") {
        out.push_back(make(LET, id, 0));
        continue;
      }
      if (lower == "func") {
        out.push_back(make(FUNC, id, 0));
        continue;
      }
      if (lower == "return") {
        out.push_back(make(RETURN, id, 0));
        continue;
      }
      if (lower == "if") {
        out.push_back(make(IF, id, 0));
        continue;
      }
      if (lower == "else") {
        out.push_back(make(ELSE, id, 0));
        continue;
      }
      if (lower == "for") {
        out.push_back(make(FOR, id, 0));
        continue;
      }
      if (lower == "in") {
        out.push_back(make(IN, id, 0));
        continue;
      }
      if (lower == "while") {
        out.push_back(make(WHILE, id, 0));
        continue;
      }
      if (lower == "struct") {
        out.push_back(make(STRUCT, id, 0));
        continue;
      }
      if (lower == "class") {
        out.push_back(make(CLASS, id, 0));
        continue;
      }
      if (lower == "sizeof") {
        out.push_back(make(SIZEOF, id, 0));
        continue;
      }
      if (lower == "break") {
        out.push_back(make(BREAK, id, 0));
        continue;
      }
      if (lower == "continue") {
        out.push_back(make(CONTINUE, id, 0));
        continue;
      }

      if (id == "Integer" || id == "Float" || id == "Boolean" ||
          id == "String" || id == "Void" || id == "Char") {
        unsigned PointerDepth = 0;
        while (Peek() == '*') {
          Consume();
          PointerDepth++;
        }
        out.push_back(make(TYPES, id, PointerDepth));
        continue;
      }
      unsigned PointerDepth = 0;
      while (Peek() == '*') {
        Consume();
        PointerDepth++;
      }

      out.push_back(make(IDENTIFIER, id, PointerDepth));
      continue;
    }

    // Two-char operators
    if (c == '=' && PeekNext() == '=') {
      Consume();
      Consume();
      out.push_back(make(EQEQ, "==", 0));
      continue;
    }
    if (c == '!' && PeekNext() == '=') {
      Consume();
      Consume();
      out.push_back(make(NOTEQ, "!=", 0));
      continue;
    }
    if (c == '<' && PeekNext() == '=') {
      Consume();
      Consume();
      out.push_back(make(LTE, "<=", 0));
      continue;
    }
    if (c == '>' && PeekNext() == '=') {
      Consume();
      Consume();
      out.push_back(make(GTE, ">=", 0));
      continue;
    }
    if (c == '&' && PeekNext() == '&') {
      Consume();
      Consume();
      out.push_back(make(AND, "&&", 0));
      continue;
    }
    if (c == '|' && PeekNext() == '|') {
      Consume();
      Consume();
      out.push_back(make(OR, "||", 0));
      continue;
    }
    if (c == '-' && PeekNext() == '>') {
      Consume();
      Consume();
      out.push_back(make(DASHGREATER, "->", 0));
      continue;
    }
    if (c == '.' && PeekNext() == '.' && PeekNextNext() == '.') {
      Consume();
      Consume();
      Consume();
      out.push_back(make(VARIDIC, "...", 0));
      continue;
    }
    if (c == '.' && PeekNext() == '.') {
      Consume();
      Consume();
      out.push_back(make(RANGE, "..", 0));
      continue;
    }

    // Single-char operators
    switch (c) {
    case '+':
      Consume();
      out.push_back(make(PLUS, "+", 0));
      break;
    case '.':
      Consume();
      out.push_back(make(DOT, ".", 0));
      break;
    case '-':
      Consume();
      out.push_back(make(MINUS, "-", 0));
      break;
    case '*':
      Consume();
      out.push_back(make(STAR, "*", 0));
      break;
    case '/':
      Consume();
      out.push_back(make(SLASH, "/", 0));
      break;
    case '=':
      Consume();
      out.push_back(make(EQ, "=", 0));
      break;
    case '<':
      Consume();
      out.push_back(make(LT, "<", 0));
      break;
    case '>':
      Consume();
      out.push_back(make(GT, ">", 0));
      break;
    case '(':
      Consume();
      out.push_back(make(LPAREN, "(", 0));
      break;
    case ')':
      Consume();
      out.push_back(make(RPAREN, ")", 0));
      break;
    case '{':
      Consume();
      out.push_back(make(LBRACE, "{", 0));
      break;
    case '}':
      Consume();
      out.push_back(make(RBRACE, "}", 0));
      break;
    case '[':
      Consume();
      out.push_back(make(LBRACKET, "[", 0));
      break;
    case ']':
      Consume();
      out.push_back(make(RBRACKET, "]", 0));
      break;
    case ':':
      Consume();
      out.push_back(make(COLON, ":", 0));
      break;
    case ',':
      Consume();
      out.push_back(make(COMMA, ",", 0));
      break;
    case ';':
      Consume();
      out.push_back(make(SEMICOLON, ";", 0));
      break;
    case '&':
      Consume();
      out.push_back(make(ANDPERCENT, "&", 0));
      break;
    default:
      Consume();
      out.push_back(make(IDENTIFIER, std::string(1, c), 0));
      break;
    }
  }

  out.push_back({EOF_TOKEN, "", 0, line, col, filename});
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
  case DOT:
    return "DOT";
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

//   let something:RandomStruct* {
	
//   }

//   )";

//   // std::string src = R"(
//   // let x:Integer;
//   // )";
//   Lexer lexer(src);
//   auto program = lexer.lexer();

//   int stmtNo = 0;
//   for (const auto &stmt : program) {
//     std::cout << "  " << std::setw(12) << tokenName(stmt.type) << " : '"
//               << stmt.value << " PointerDepth=" << std::to_string(stmt.ptrdepth)
//               << "'\n";
//   }
// }
