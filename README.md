GigaLang

GigaLang is still in development and is being written in C++ with an LLVM backend.

I would probably accept pull requests, but don’t get your hopes too high—you might not be able to read the code yet.

Big compiler developers, please don’t yell at me. I’m still a small compiler developer.

My dream is to make this compiler self-hosted. I know it’s a big dream, but who cares—you’re just dreaming too.

Program Structure
program         ::= header_section? { statement ";" }

header_section  ::= { header_line ";" }
header_line     ::= "@" ( "version" | "author" | "import" | "syscall" ) header_value
header_value    ::= string_literal | syscall_signature

// Syscall signature
syscall_signature ::= identifier "(" [ param_list ] ")"

// Statements
statement       ::= var_decl
                  | assignment
                  | func_decl
                  | if_stmt
                  | for_stmt
                  | while_stmt
                  | class_decl
                  | expr_stmt
                  | print_stmt

// Variable declaration & assignment
var_decl        ::= "let" identifier ":" type "=" expr
assignment      ::= identifier "=" expr

// Types
type            ::= "Integer" | "Float" | "Boolean" | identifier | "Void" | "Char" | array_type
array_type      ::= type "[" [ integer_literal ] "]"

// Expressions
expr            ::= literal
                  | identifier
                  | expr binary_op expr
                  | func_call
                  | "(" expr ")"

binary_op       ::= "+" | "-" | "*" | "/" | "==" | "!=" | "<" | ">" | "<=" | ">=" | "&&" | "||"

literal         ::= integer_literal
                  | float_literal
                  | boolean_literal
                  | string_literal

// Functions
func_decl       ::= "func" identifier "(" [ param_list ] ")" ["->" type] "{" { statement ";" } "}"
param_list      ::= param { "," param }
param           ::= identifier ":" type
func_call       ::= identifier "(" [ arg_list ] ")"
arg_list        ::= expr { "," expr }

// Conditionals
if_stmt         ::= "if" expr "{" { statement ";" } "}" [ "else" "{" { statement ";" } "}" ]

// Loops
for_stmt ::= "for" "(" (var_decl | assignment) ";"
                         expr ";"
                         assignment ")"
              "{" { statement ";" } "}"

while_stmt      ::= "while" expr "{" { statement ";" } "}"

// Identifiers and literals
identifier      ::= letter { letter | digit | "_" }
integer_literal ::= digit { digit }
float_literal   ::= digit { digit } "." digit { digit }
boolean_literal ::= "true" | "false"
string_literal  ::= '"' { any_character } '"'

letter          ::= "a".."z" | "A".."Z"
digit           ::= "0".."9"
any_character   ::= ? any valid character ?
