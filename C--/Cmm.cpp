/* * * * * * * * * * * * * * * * * * * *\
|  		    C--                 |
\* * * * * * * * * * * * * * * * * * * */

%{
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <map>

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/BasicBlock.h"

#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/SystemUtils.h"
#include "llvm/Support/ToolOutputFile.h"

#include <memory>
#include <algorithm>
#include <list>
#include <vector>
#include <utility>
#include <stack>

#include "symbol.h"

using namespace llvm;
using namespace std;

using parameter = pair<Type*,const char*>;
using parameter_list = std::list<parameter>;

typedef struct {
  BasicBlock* expr;
  BasicBlock* body;
  BasicBlock* reinit;
  BasicBlock* exit;
} loop_info;

stack<loop_info> loop_stack;

int num_errors;

extern int yylex();   /* lexical analyzer generated from lex.l */

int yyerror(const char *error);
int parser_error(const char*);

void cmm_abort();
char *get_filename();
int get_lineno();

int loops_found=0;

extern Module *M;
extern LLVMContext TheContext;

Function *Fun;
IRBuilder<> *Builder;

Value* BuildFunction(Type* RetType, const char *name,
			   parameter_list *params);

%}

/* Data structure for tree nodes*/

%union {
  int inum;
  char * id;
  Type*  type;
  Value* value;
  parameter_list *plist;
  vector<Value*> *arglist;
  BasicBlock *bb;
}

/* these tokens are simply their corresponding int values, more terminals*/

%token SEMICOLON COMMA MYEOF
%token LBRACE RBRACE LPAREN RPAREN LBRACKET RBRACKET

%token ASSIGN PLUS MINUS STAR DIV MOD
%token LT GT LTE GTE EQ NEQ
%token BITWISE_OR BITWISE_XOR LSHIFT RSHIFT BITWISE_INVERT
%token DOT AMPERSAND

%token FOR WHILE IF ELSE DO RETURN SWITCH
%token BREAK CONTINUE CASE COLON
%token INT VOID BOOL
%token I2P P2I SEXT ZEXT

/* NUMBER and ID have values associated with them returned from lex*/

%token <inum> CONSTANT_INTEGER /*data type of NUMBER is num union*/
%token <id>  ID

%left EQ NEQ LT GT LTE GTE
%left BITWISE_OR
%left BITWISE_XOR
%left AMPERSAND
%left LSHIFT RSHIFT
%left PLUS MINUS
%left MOD DIV STAR
%nonassoc ELSE

%type <type> type_specifier

%type <value> opt_initializer
%type <value> expression bool_expression
%type <value> lvalue_location primary_expression unary_expression
%type <value> constant constant_expression unary_constant_expression

%type <value> statement selection_stmt iteration_stmt return_stmt
%type <value> assign_expression argument_list_opt

%type <plist> param_list param_list_opt

%%

translation_unit:	  external_declaration
			| translation_unit external_declaration
                        | translation_unit MYEOF
{
  YYACCEPT;
}
;

external_declaration:	  function_definition
                        | global_declaration
;

function_definition:	  type_specifier ID LPAREN param_list_opt RPAREN
// NO MODIFICATION NEEDED
{
  symbol_push_scope();
  BuildFunction($1,$2,$4);
}
compound_stmt
{
  symbol_pop_scope();
}

| type_specifier STAR ID LPAREN param_list_opt RPAREN
{
  symbol_push_scope();
  BuildFunction(PointerType::get($1,0),$3,$5);
}
compound_stmt
{
  symbol_pop_scope();
}
;

global_declaration:    type_specifier STAR ID opt_initializer SEMICOLON
{
  // Check to make sure global isn't already allocated
  // new GlobalVariable(...)
}
| type_specifier ID opt_initializer SEMICOLON
{
  // Check to make sure global isn't already allocated
  // new GlobalVariable(...)
}
;

opt_initializer:   ASSIGN constant_expression { $$ = $2; } | { $$ = nullptr; } ;

// NO MODIFICATION NEEDED
type_specifier:		  INT
{
  $$ = Type::getInt64Ty(TheContext);
}
                     |    VOID
{
  $$ = Type::getVoidTy(TheContext);
}
;


param_list_opt:
{
  $$ = nullptr;
}
| param_list
{
  $$ = $1;
}
;

// USED FOR FUNCTION DEFINITION
param_list:
param_list COMMA type_specifier ID
{
  $$ = $1;
  $$->push_back(parameter($3,$4));
}
| param_list COMMA type_specifier STAR ID
{
  $$ = $1;
  $$->push_back(parameter(PointerType::get($3,0),$5));
}
| type_specifier ID
{
  $$ = new parameter_list;
  $$->push_back(parameter($1,$2));
}
| type_specifier STAR ID
{
  $$ = new parameter_list;
  $$->push_back( parameter(PointerType::get($1,0),$3));
}
;


statement:		  expr_stmt
			| compound_stmt
			| selection_stmt
			| iteration_stmt
			| return_stmt
                        | break_stmt
                        | continue_stmt
                        | case_stmt
;

expr_stmt:	           SEMICOLON
			|  assign_expression SEMICOLON
;

local_declaration:    type_specifier STAR ID opt_initializer SEMICOLON
{
  Value * ai = Builder->CreateAlloca(PointerType::get($1,0),0,$3); //allocate space
  if (nullptr != $4)
    Builder->CreateStore($4,ai); //if nullptr is not equal to ID, store in address of ID
  symbol_insert($3,ai);
}
| type_specifier ID opt_initializer SEMICOLON
{
  Value * ai = Builder->CreateAlloca($1,0,$2);
  if (nullptr != $3)
    Builder->CreateStore($3,ai); //if nullptr is not equal to opt_initializer, store in address of opt_initializer
  symbol_insert($2,ai);
}
;

local_declaration_list:	   local_declaration
                         | local_declaration_list local_declaration
;

local_declaration_list_opt:
			| local_declaration_list
;

compound_stmt:		  LBRACE {

  symbol_push_scope(); //push scope to record variables within compound statement
}
local_declaration_list_opt
statement_list_opt
{

  symbol_pop_scope(); //pop scope to remove variables no longer accessible
}
RBRACE
;


statement_list_opt:
			| statement_list
;

statement_list:		statement
		      | statement_list statement
;

break_stmt:               BREAK SEMICOLON
;

case_stmt:                CASE constant_expression COLON
;

continue_stmt:            CONTINUE SEMICOLON
;

selection_stmt:
  IF LPAREN bool_expression RPAREN
 {
  // make building blocks with BuildingBlock::Create(M->getContext(), "if.then", Fun)
  // push_loop(NULL, all three BBs)
  BasicBlock* body = BasicBlock::Create(M->getContext(), "if.then", Fun);
  BasicBlock* reinit = BasicBlock::Create(M->getContext(), "if.body", Fun);
  BasicBlock* exit = BasicBlock::Create(M->getContext(), "if.exit", Fun);
  loop_info info = {NULL, body, reinit, exit};
  loop_stack.push(info);
  Value* val = Builder->CreateICmpNE($3, Builder->getInt64(0), "icmp.if"); //compare bool_expression with 0
  Builder->CreateCondBr(val, body, reinit); //create a conditional branch
  Builder->SetInsertPoint(body); //specify body should be appended to the end of this block
 }
 statement
 {
  loop_info curr = loop_stack.top();
  Builder->CreateBr(curr.exit);
  Builder->SetInsertPoint(curr.exit);
 } ELSE statement
 {
  loop_info curr = loop_stack.top();
  Builder->CreateBr(curr.exit);
  Builder->SetInsertPoint(curr.exit); //specify curr.exit should be appended to the end of this block
  loop_stack.pop();
 }
| SWITCH LPAREN expression RPAREN statement {

}
;


iteration_stmt: WHILE
{
  BasicBlock* expr = BasicBlock::Create(M->getContext(), "w.expr", Fun);
  Builder->CreateBr(expr);
  Builder->SetInsertPoint(expr);
  $<bb>$ = expr;
}
LPAREN bool_expression RPAREN
{
  BasicBlock* body = BasicBlock::Create(M->getContext(), "w.body", Fun);
  BasicBlock* exit = BasicBlock::Create(M->getContext(), "w.exit", Fun);
  Builder->CreateCondBr(Builder->CreateICmpNE($4, Builder->getInt64(0)), body, exit); //create a conditional branch
  Builder->SetInsertPoint(body); //specify body should be appended to the end of this block
  $<bb>$ = exit;
}
statement
{
  Builder->CreateBr($<bb>2);
  Builder->SetInsertPoint($<bb>6);
}
| FOR LPAREN expr_opt SEMICOLON
{
/*
  BasicBlock* expr = BasicBlock::Create(M->getContext(), "f.expr", Fun);
  BasicBlock* body = BasicBlock::Create(M->getContext(), "f.body", Fun);
  BasicBlock* incr = BasicBlock::Create(M->getContext(), "f.incr", Fun);
  BasicBlock* exit = BasicBlock::Create(M->getContext(), "f.exit", Fun);
  loop_stack.push({expr, body, incr, exit});
  Builder->CreateBr(expr); //for
  Builder->SetInsertPoint(expr);
*/
}
bool_expression SEMICOLON
{
/*
  loop_info loop = loop_stack.top();
  Value* val = Builder->CreateICmpNE($5, Builder->getInt64(0), "f.bool");
  Builder->CreateCondBr(val, loop.body, loop.exit);
  Builder->SetInsertPoint(loop.reinit);
*/
}
expr_opt
{
/*
  loop_info loop = loop_stack.top();
  Builder->CreateBr(loop.expr);
  Builder->SetInsertPoint(loop.body);
*/
}
RPAREN statement
{
/*
  loop_info loop = loop_stack.top();
  Builder->CreateBr(loop.reinit);
  Builder->SetInsertPoint(loop.exit);
  loop_stack.pop();
  */
}
| DO statement WHILE LPAREN bool_expression RPAREN SEMICOLON
;

expr_opt:
	| assign_expression
;

// Return statement
return_stmt:		  RETURN SEMICOLON
{
 //return create retvoid isntead of null_ptr
 $$ = Builder->CreateRetVoid();
}
| RETURN expression SEMICOLON
{
 // return expression as a return ins
 $$ = Builder->CreateRet($2);
}
;

bool_expression: expression
{
  $$ = $1;
}
;

// Do this
assign_expression:
  lvalue_location ASSIGN expression
{
  $$ = Builder->CreateStore($3, $1);
}
| expression
{

}
;

expression: unary_expression
{
  $$ = $1;
}
| expression BITWISE_OR expression
{
  //bitwise_or operation in LLVM
  $$ = Builder->CreateOr($1,$3);
}
| expression BITWISE_XOR expression
{
  $$ = Builder->CreateXor($1,$3);
}
| expression AMPERSAND expression
{
  $$ = Builder->CreateAnd($1,$3);
}
| expression EQ expression
{
  Value* val = Builder->CreateICmpEQ($1,$3);
  $$ = Builder->CreateSelect(val, Builder->getInt64(1), Builder->getInt64(0));
  // return second if true, third if false
}
| expression NEQ expression
{
  Value* val = Builder->CreateICmpNE($1,$3);
  $$ = Builder->CreateSelect(val, Builder->getInt64(1), Builder->getInt64(0));
  // return second if true, third if false
}
| expression LT expression
{
  Value* val = Builder->CreateICmpSLT($1,$3);
  $$ = Builder->CreateSelect(val, Builder->getInt64(1), Builder->getInt64(0));
  // return second if true, third if false
}
| expression GT expression
{
  Value* val = Builder->CreateICmpSGT($1,$3);
  $$ = Builder->CreateSelect(val, Builder->getInt64(1), Builder->getInt64(0));
  // return second if true, third if false
}
| expression LTE expression
{
  Value* val = Builder->CreateICmpSLE($1,$3);
  $$ = Builder->CreateSelect(val, Builder->getInt64(1), Builder->getInt64(0));
  // return second if true, third if false
}
| expression GTE expression
{
  Value* val = Builder->CreateICmpSGE($1,$3);
  $$ = Builder->CreateSelect(val, Builder->getInt64(1), Builder->getInt64(0));
  // return second if true, third if false
}
| expression LSHIFT expression
{
  $$ = Builder->CreateShl($1,$3);
}
| expression RSHIFT expression
{
  $$ = Builder->CreateAShr($1,$3);
}
| expression PLUS expression
{
  $$ = Builder->CreateAdd($1,$3);
}
| expression MINUS expression
{
  $$ = Builder->CreateSub($1,$3);
}
| expression STAR expression
{
  $$ = Builder->CreateMul($1,$3);
}
| expression DIV expression
{
  $$ = Builder->CreateSDiv($1,$3);
}
| expression MOD expression
{
  $$ = Builder->CreateSRem($1,$3);
}
| BOOL LPAREN expression RPAREN
{
  $$ = $3;
}
| I2P LPAREN expression RPAREN
{
  Value* val = $3;
  $$ = Builder->CreateIntToPtr(val, Type::getVoidTy(TheContext));
}
| P2I LPAREN expression RPAREN
{
  Value* val = $3;
  $$ = Builder->CreatePtrToInt(val, Type::getVoidTy(TheContext));
}
| ZEXT LPAREN expression RPAREN
{
  Value* val = $3;
  $$ = Builder->CreateZExt(val, Type::getInt64Ty(TheContext));
}
| SEXT LPAREN expression RPAREN
{
  Value* val = $3;
  $$ = Builder->CreateSExt(val, Type::getInt64Ty(TheContext));
}
| ID LPAREN argument_list_opt RPAREN
{
  $$ = Builder->CreateCall(M->getFunction($1), makeArrayRef($3));
}
| LPAREN expression RPAREN
{
  $$ = $2;
}
;


argument_list_opt: | argument_list
;

argument_list:
  expression
| argument_list COMMA expression
;


unary_expression: primary_expression
{
  $$ = $1;
}
| AMPERSAND primary_expression
| STAR primary_expression
| MINUS unary_expression //do this
{
  $$ = Builder->CreateNeg($2);
}
| PLUS unary_expression
{
  $$ = $2;
}
| BITWISE_INVERT unary_expression
{
  $$ = Builder->CreateNot($2);
}
;

primary_expression:
  lvalue_location
  {
    $$ = Builder->CreateLoad($1);
  }
| constant
{
  $$ = $1;
}
;

lvalue_location:
  ID
{
  $$ = symbol_find($1);
}
| lvalue_location LBRACKET expression RBRACKET
| STAR LPAREN expression RPAREN
;

constant_expression:
  unary_constant_expression
  {
    $$ = $1;
  }
| constant_expression BITWISE_OR expression
{
  //bitwise_or operation in LLVM
  $$ = Builder->CreateOr($1,$3);
}
| constant_expression BITWISE_XOR expression
{
  $$ = Builder->CreateXor($1,$3);
}
| constant_expression AMPERSAND expression
{
  $$ = Builder->CreateAnd($1,$3);
}
| constant_expression EQ expression
{
  Value* val = Builder->CreateICmpEQ($1,$3);
  $$ = Builder->CreateSelect(val, Builder->getInt64(1), Builder->getInt64(0));
  // return second if true, third if false
}
| constant_expression NEQ expression
{
  Value* val = Builder->CreateICmpNE($1,$3);
  $$ = Builder->CreateSelect(val, Builder->getInt64(1), Builder->getInt64(0));
  // return second if true, third if false
}
| constant_expression LT expression
{
  Value* val = Builder->CreateICmpSLT($1,$3);
  $$ = Builder->CreateSelect(val, Builder->getInt64(1), Builder->getInt64(0));
  // return second if true, third if false
}
| constant_expression GT expression
{
  Value* val = Builder->CreateICmpSGT($1,$3);
  $$ = Builder->CreateSelect(val, Builder->getInt64(1), Builder->getInt64(0));
  // return second if true, third if false
}
| constant_expression LTE expression
{
  Value* val = Builder->CreateICmpSLE($1,$3);
  $$ = Builder->CreateSelect(val, Builder->getInt64(1), Builder->getInt64(0));
  // return second if true, third if false
}
| constant_expression GTE expression
{
  Value* val = Builder->CreateICmpSGE($1,$3);
  $$ = Builder->CreateSelect(val, Builder->getInt64(1), Builder->getInt64(0));
  // return second if true, third if false
}
| constant_expression LSHIFT expression
{
  $$ = Builder->CreateShl($1,$3);
}

| constant_expression RSHIFT expression
{
  $$ = Builder->CreateAShr($1,$3);
}

| constant_expression PLUS expression
{
  $$ = Builder->CreateAdd($1,$3);
}
| constant_expression MINUS expression
