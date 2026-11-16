GigaLang

GigaLang is still in development and is being written in C++ with an LLVM backend.

I would probably accept pull requests, but don’t get your hopes too high—you might not even be able to read the code yet.

Big compiler developers, please don’t yell at me. I’m still a small compiler developer.

My dream is to make this compiler self-hosted. I know it’s a big dream, but who cares—you’re just dreaming too.

```ebnf
(* ========================= *)
(* PROGRAM STRUCTURE          *)
(* ========================= *)

Program           = { Statement | ";" } ;

Statement         = VariableDeclaration
                  | FunctionDeclaration
                  | StructDeclaration
                  | ReturnStatement
                  | IfStatement
                  | WhileStatement
                  | ForStatement
                  | CompoundStatement
                  | BreakStatement
                  | ContinueStatement
                  | Expression ;


(* ========================= *)
(* DECLARATIONS               *)
(* ========================= *)

VariableDeclaration
                  = "let" Identifier ":" Type
                    [ "=" Expression ] ";" ;

FunctionDeclaration
                  = "func" Identifier "(" [ ParameterList ] ")"
                    [ "->" Type ]
                    Statement ;

ParameterList     = Parameter
                    { "," Parameter }
                    [ "," "..." ] ;

Parameter         = Identifier ":" Type ;

StructDeclaration = "struct" Identifier
                    "[" FieldList "]" ";" ;

FieldList         = FieldDeclaration
                    { "," FieldDeclaration } ;

FieldDeclaration  = Identifier ":" Type ;


(* ========================= *)
(* TYPES                      *)
(* ========================= *)

Type              = { "*" } TypeName [ "[" Expression "]" ] ;

TypeName          = PrimitiveType
                  | Identifier ;

PrimitiveType     = SignedIntegerType
                  | UnsignedIntegerType
                  | FloatingPointType
                  | OtherPrimitiveType ;

SignedIntegerType = "S8" | "S16" | "S32" | "S64" ;

UnsignedIntegerType
                  = "U8" | "U16" | "U32" | "U64" ;

FloatingPointType = FloatTypeToken ;

OtherPrimitiveType
                  = OtherTypeToken ;


(* ========================= *)
(* STATEMENTS                 *)
(* ========================= *)

CompoundStatement = "{" { Statement | ";" } "}" ;

ReturnStatement   = "return" [ Expression ] ";" ;

IfStatement      = "if" "(" Expression ")" Statement
                   [ "else" Statement ] ;

WhileStatement   = "while" Expression Statement ;

ForStatement     = "for" "(" Expression ";"
                   Expression ";"
                   Expression ")"
                   Statement ;

BreakStatement   = "break" ;

ContinueStatement = "continue" ;


(* ========================= *)
(* EXPRESSIONS                *)
(* ========================= *)

Expression        = AssignmentExpression
                    { "&&" ComparisonExpression } ;

AssignmentExpression
                  = ComparisonExpression
                    [ "=" Expression ] ;

ComparisonExpression
                  = AdditiveExpression
                    { ComparisonOperator AdditiveExpression } ;

ComparisonOperator
                  = "==" | "!="
                  | "<"  | ">"
                  | "<=" | ">=" ;

AdditiveExpression
                  = MultiplicativeExpression
                    { ("+" | "-") MultiplicativeExpression } ;

MultiplicativeExpression
                  = FieldExpression
                    { ("*" | "/") FieldExpression } ;

FieldExpression   = Factor
                    { ("." | "->") Identifier } ;


(* ========================= *)
(* FACTORS                    *)
(* ========================= *)

Factor            = IntegerLiteral
                  | FloatLiteral
                  | BooleanLiteral
                  | StringLiteral
                  | CharacterLiteral
                  | ParenthesizedExpression
                  | CastExpression
                  | ArrayLiteral
                  | FunctionCall
                  | ArrayAccess
                  | VariableReference
                  | SizeOfExpression
                  | SyscallExpression
                  | AddressOfExpression
                  | DereferenceExpression ;

ParenthesizedExpression
                  = "(" Expression ")" ;

CastExpression    = "(" Type ")" Expression ;

ArrayLiteral      = "[" [ Expression
                    { "," Expression } ] "]" ;

FunctionCall      = Identifier "(" [ ArgumentList ] ")" ;

ArgumentList      = Expression { "," Expression } ;

ArrayAccess       = Identifier "[" Expression "]" ;

VariableReference = Identifier ;

SizeOfExpression  = "sizeof" "(" Expression ")" ;

SyscallExpression = "@Syscall" "(" IntegerLiteral
                    "," ArgumentList ")" ;

AddressOfExpression
                  = "&%" Expression ;

DereferenceExpression
                  = "*" Identifier
                    [ "[" Expression "]" ] ;


(* ========================= *)
(* LEXICAL ELEMENTS           *)
(* ========================= *)

Identifier        = IdentifierStart { IdentifierPart } ;

IntegerLiteral    = Digit { Digit } ;

FloatLiteral      = Digit { Digit } "." Digit { Digit } ;

BooleanLiteral    = "true" | "false" ;

StringLiteral     = '"' { StringCharacter } '"' ;

CharacterLiteral  = "'" Character "'" ;

Digit             = "0" | "1" | "2" | "3" | "4"
                  | "5" | "6" | "7" | "8" | "9" ;

IdentifierStart   = Letter | "_" ;

IdentifierPart    = Letter | Digit | "_" ;

Letter            = "A"…"Z" | "a"…"z" ;
```
