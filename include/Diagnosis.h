#pragma once
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// ANSI color helpers
namespace Color {
inline const char *RESET = "\033[0m";
inline const char *BOLD = "\033[1m";
inline const char *RED = "\033[1;31m";
inline const char *YELLOW = "\033[1;33m";
inline const char *CYAN = "\033[1;36m";
inline const char *WHITE = "\033[1;37m";
inline const char *GREEN = "\033[1;32m";
} // namespace Color

enum class DiagLevel { Note, Warning, Error, Fatal };

struct SourceLoc {
  std::string file;
  unsigned line;
  unsigned col;
};

class Diagnostics {
public:
  explicit Diagnostics(const std::vector<std::string> &sourceLines)
      : lines(sourceLines) {}

  // Main emit function
  void emit(DiagLevel level, const SourceLoc &loc, const std::string &msg,
            const std::string &hint = "") {

    // ── header:  file:line:col: severity: message
    std::cerr << Color::WHITE << loc.file << ":" << loc.line << ":" << loc.col
              << ": " << Color::RESET;

    switch (level) {
    case DiagLevel::Error:
      std::cerr << Color::RED << "error: ";
      break;
    case DiagLevel::Warning:
      std::cerr << Color::YELLOW << "warning: ";
      break;
    case DiagLevel::Note:
      std::cerr << Color::CYAN << "note: ";
      break;
    case DiagLevel::Fatal:
      std::cerr << Color::RED << "fatal: ";
      break;
    }
    std::cerr << Color::WHITE << msg << Color::RESET << "\n";

    // ── source line snippet
    if (loc.line >= 1 && loc.line <= lines.size()) {
      const std::string &srcLine = lines[loc.line - 1];
      std::cerr << "  " << srcLine << "\n";

      // ── caret  ^~~~~
      std::cerr << Color::GREEN << "  ";
      for (unsigned i = 1; i < loc.col; ++i)
        std::cerr << ' ';
      std::cerr << "^";
      std::cerr << Color::RESET << "\n";
    }

    // ── optional fix-it hint
    if (!hint.empty()) {
      std::cerr << Color::CYAN << "  hint: " << hint << Color::RESET << "\n";
    }

    if (level == DiagLevel::Error || level == DiagLevel::Fatal)
      errorCount++;
    if (level == DiagLevel::Fatal)
      throw FatalError(msg);
  }

  // Convenience wrappers
  void error(const SourceLoc &loc, const std::string &msg,
             const std::string &hint = "") {
    emit(DiagLevel::Error, loc, msg, hint);
  }
  void warning(const SourceLoc &loc, const std::string &msg,
               const std::string &hint = "") {
    emit(DiagLevel::Warning, loc, msg, hint);
  }
  void note(const SourceLoc &loc, const std::string &msg) {
    emit(DiagLevel::Note, loc, msg);
  }

  bool hasErrors() const { return errorCount > 0; }
  unsigned getErrorCount() const { return errorCount; }

  struct FatalError : std::exception {
    std::string msg;
    explicit FatalError(std::string m) : msg(std::move(m)) {}
    const char *what() const noexcept override { return msg.c_str(); }
  };

private:
  const std::vector<std::string> &lines;
  unsigned errorCount = 0;
};
