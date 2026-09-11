#include <Diagnosis.h>
#include <ast.h>
#include <fstream>
<<<<<<< HEAD
#include <iostream>
#include <lexer.h>
#include <parser.h>
#include <sstream>
#include <string>
#include <vector>

#include "llvm/TargetParser/Host.h"
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Host.h>

struct Options {
  std::string inputFile;
  std::string outputName = "output";

  bool printIR = false;
  bool emitIR = false;
  bool emitASM = false;
  bool emitObj = false;
  bool buildExe = true;
};

Options parseArgs(int argc, char **argv) {
  Options opt;

  if (argc < 2)
    throw std::runtime_error("Usage: ./myprogram <file> [options]");

  opt.inputFile = argv[1];

  for (int i = 2; i < argc; i++) {
    std::string arg = argv[i];

    if (arg == "-ir") {
      opt.printIR = true;
      opt.buildExe = false;

    } else if (arg == "--emit-ir") {
      opt.emitIR = true;
      opt.buildExe = false;
      if (i + 1 < argc)
        opt.outputName = argv[++i];

    } else if (arg == "--emit-asm") {
      opt.emitASM = true;
      opt.buildExe = false;
      if (i + 1 < argc)
        opt.outputName = argv[++i];

    } else if (arg == "--emit-obj") {
      opt.emitObj = true;
      opt.buildExe = false;
      if (i + 1 < argc)
        opt.outputName = argv[++i];

    } else if (arg == "-o") {
      if (i + 1 < argc)
        opt.outputName = argv[++i];
    }
  }

  return opt;
}

std::string readFile(const std::string &path) {
  std::ifstream file(path);
  if (!file.is_open())
    throw std::runtime_error("Cannot open file");

  std::stringstream ss;
  ss << file.rdbuf();
  return ss.str();
}

void emitIR(llvm::Module *module, const std::string &file) {
  std::error_code EC;
  llvm::raw_fd_ostream out(file, EC, llvm::sys::fs::OF_None);
  if (EC)
    throw std::runtime_error(EC.message());

  module->print(out, nullptr);
}

static llvm::TargetMachine *createTargetMachine(llvm::Module *module) {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  std::string triple = llvm::sys::getDefaultTargetTriple();
  module->setTargetTriple(triple);

  std::string err;
  auto target = llvm::TargetRegistry::lookupTarget(triple, err);
  if (!target)
    throw std::runtime_error(err);

  llvm::TargetOptions opt;
  auto RM = std::optional<llvm::Reloc::Model>();

  auto tm = target->createTargetMachine(triple, "generic", "", opt, RM);

  module->setDataLayout(tm->createDataLayout());
  return tm;
}

void emitASM(llvm::Module *module, const std::string &file) {
  auto tm = createTargetMachine(module);

  std::error_code EC;
  llvm::raw_fd_ostream out(file, EC, llvm::sys::fs::OF_None);
  if (EC)
    throw std::runtime_error(EC.message());

  llvm::legacy::PassManager pass;
  if (tm->addPassesToEmitFile(pass, out, nullptr,
                              llvm::CodeGenFileType::AssemblyFile))
    throw std::runtime_error("ASM emission failed");

  pass.run(*module);
}

void emitObject(llvm::Module *module, const std::string &file) {
  auto tm = createTargetMachine(module);

  std::error_code EC;
  llvm::raw_fd_ostream out(file, EC, llvm::sys::fs::OF_None);
  if (EC)
    throw std::runtime_error(EC.message());

  llvm::legacy::PassManager pass;
  if (tm->addPassesToEmitFile(pass, out, nullptr,
                              llvm::CodeGenFileType::ObjectFile))
    throw std::runtime_error("Object emission failed");

  pass.run(*module);
}

void linkExe(const std::string &obj, const std::string &exe) {
  std::string cmd = "clang " + obj + " -no-pie -o " + exe;
  if (system(cmd.c_str()) != 0)
    throw std::runtime_error("link failed");
}

int main(int argc, char **argv) {
  try {
    Options opt = parseArgs(argc, argv);

    std::string src = readFile(opt.inputFile);

    std::vector<std::string> lines;
    std::istringstream ss(src);
    std::string line;
    while (std::getline(ss, line))
      lines.push_back(line);

    Diagnostics diag(lines);

    Lexer lexer(src);
    auto program = lexer.lexer();

    Parser parser(program, "MYMODULE", diag);
    auto ast = parser.Parse();

    auto &cc = parser.getCodegenContext();

    for (auto &n : ast)
      n->codegen(cc);

    llvm::Module *module = cc.Module.get();

    if (opt.printIR) {
      module->print(llvm::outs(), nullptr);
      return 0;
    }

    std::string base = opt.outputName;
    std::string irFile = base + ".ll";
    std::string asmFile = base + ".s";
    std::string objFile = base + ".o";

    emitIR(module, irFile);

    if (opt.emitIR)
      return 0;

    if (opt.emitASM) {
      emitASM(module, asmFile);
      return 0;
    }

    if (opt.emitObj) {
      emitObject(module, objFile);
      return 0;
    }

    // full pipeline
    emitObject(module, objFile);
    linkExe(objFile, base);

    std::cout << "Built: " << base << "\n";

  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
=======
#include <lexer.h>
#include <llvm-18/llvm/Support/FileSystem.h>
#include <parser.h>
#include <string>

struct Options {
  std::string inputFile;
  std::string outputName = "output";

  bool printIR = false;
  bool emitIR = false;
  bool emitObj = false;
  bool buildExe = true;
};

Options parseArgs(int argc, char **argv) {
  Options opt;

  if (argc < 2) {
    throw std::runtime_error("Usage: ./program <file> [options]");
  }

  opt.inputFile = argv[1];

  for (int i = 2; i < argc; i++) {
    std::string arg = argv[i];

    if (arg == "--ir") {
      opt.printIR = true;
      opt.buildExe = false;
    } else if (arg == "--emit-ir") {
      opt.emitIR = true;
      opt.buildExe = false;
      if (i + 1 < argc)
        opt.outputName = argv[++i];
    } else if (arg == "--emit-obj") {
      opt.emitObj = true;
      opt.buildExe = false;
      if (i + 1 < argc)
        opt.outputName = argv[++i];
    } else if (arg == "-o") {
      if (i + 1 < argc)
        opt.outputName = argv[++i];
    }
  }

  return opt;
}

std::string readFile(const std::string &path) {
  std::ifstream file(path);
  if (!file.is_open())
    throw std::runtime_error("Cannot open file: " + path);

  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

void emitIR(llvm::Module *module, const std::string &path) {
  std::error_code EC;
  llvm::raw_fd_ostream out(path, EC, llvm::sys::fs::OpenFlags::OF_None);

  if (EC)
    throw std::runtime_error("IR file error: " + EC.message());

  module->print(out, nullptr);
}

void emitObject(const std::string &irFile, const std::string &outObj) {
  std::string cmd = "llc " + irFile + " -filetype=obj -o " + outObj;
  if (system(cmd.c_str()) != 0)
    throw std::runtime_error("llc failed");
}

void linkExecutable(const std::string &objFile, const std::string &exe) {
  std::string cmd = "clang " + objFile + " -o " + exe;
  if (system(cmd.c_str()) != 0)
    throw std::runtime_error("linking failed");
}
int main(int argc, char **argv) {
  try {
    Options opt = parseArgs(argc, argv);

    std::string src = readFile(opt.inputFile);

    std::vector<std::string> sourceLines;
    {
      std::istringstream ss(src);
      std::string line;
      while (std::getline(ss, line))
        sourceLines.push_back(line);
    }

    Diagnostics diag(sourceLines);

    Lexer lexer(src);
    auto program = lexer.lexer();

    Parser parser(program, "MYMODULE", diag);
    auto astNodes = parser.Parse();

    auto &cc = parser.getCodegenContext();

    for (auto &node : astNodes) {
      node->codegen(cc);
    }

    llvm::Module *module = cc.Module.get();

    // ---------------- EMIT IR ----------------
    if (opt.emitIR) {
      emitIR(module, opt.outputName + ".ll");
      return 0;
    }

    // ---------------- FULL PIPELINE ----------------
    std::string irFile = opt.outputName + ".ll";
    std::string objFile = opt.outputName + ".o";
    std::string exeFile = opt.outputName;

    emitIR(module, irFile);

    if (opt.emitObj) {
      emitObject(irFile, objFile);
      return 0;
    }

    if (opt.buildExe) {
      emitObject(irFile, objFile);
      linkExecutable(objFile, exeFile);
      std::cout << "Built: " << exeFile << "\n";
    }

  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }

  return 0;
>>>>>>> again
}
