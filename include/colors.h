#pragma once
#include <iostream>

namespace Colors {
    inline const char* RESET  = "\033[0m";
    inline const char* RED    = "\033[31m";
    inline const char* GREEN  = "\033[32m";
    inline const char* YELLOW = "\033[33m";
    inline const char* BLUE   = "\033[34m";
    inline const char* MAGENTA= "\033[35m";
    inline const char* CYAN   = "\033[36m";
    inline const char* WHITE  = "\033[37m";
    inline const char* BOLD   = "\033[1m";

    inline void printRed(const std::string& text)    { std::cout << RED << text << RESET; }
    inline void printGreen(const std::string& text)  { std::cout << GREEN << text << RESET; }
    inline void printYellow(const std::string& text) { std::cout << YELLOW << text << RESET; }
    inline void printBlue(const std::string& text)   { std::cout << BLUE << text << RESET; }
    inline void printMagenta(const std::string& text){ std::cout << MAGENTA << text << RESET; }
    inline void printCyan(const std::string& text)   { std::cout << CYAN << text << RESET; }
    inline void printWhite(const std::string& text)  { std::cout << WHITE << text << RESET; }
    inline void printBold(const std::string& text)   { std::cout << BOLD << text << RESET; }
}

