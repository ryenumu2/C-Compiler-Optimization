%{
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/IRBuilder.h"

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

// NO MODIFICATION NEEDED
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

// YOU MUST FIXME: hacked to prevent segfault on initial testing
opt_initializer:   ASSIGN constant_expression { $$ = nullptr; } | { $$ = nullptr; } ;

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

// USED FOR FUNCTION DEFINITION; NO MODIFICATION NEEDED
param_list:	
param_list COMMA type_specifier ID
{
  $$ = $1;
  $$->push_back( parameter($3,$4) );
}
| param_list COMMA type_specifier STAR ID
{
  $$ = $1;
  $$->push_back( parameter(PointerType::get($3,0),$5) );
}
| type_specifier ID
{
  $$ = new parameter_list;
  $$->push_back( parameter($1,$2) );
}
| type_specifier STAR ID
{
  $$ = new parameter_list;
  $$->push_back( parameter(PointerType::get($1,0),$3) );
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
  Value * ai = Builder->CreateAlloca(PointerType::get($1,0),0,$3);
  if (nullptr != $4)
    Builder->CreateStore($4,ai);
  symbol_insert($3,ai);
}
| type_specifier ID opt_initializer SEMICOLON
{
  Value * ai = Builder->CreateAlloca($1,0,$2);
  if (nullptr != $3)
    Builder->CreateStore($3,ai);
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
  // PUSH SCOPE TO RECORD VARIABLES WITHIN COMPOUND STATEMENT
  symbol_push_scope();
}
local_declaration_list_opt
statement_list_opt 
{
  // POP SCOPE TO REMOVE VARIABLES NO LONGER ACCESSIBLE
  symbol_pop_scope();
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
  IF LPAREN bool_expression RPAREN statement ELSE statement
| SWITCH LPAREN expression RPAREN statement 
;


iteration_stmt:
  WHILE LPAREN bool_expression RPAREN statement
| FOR LPAREN expr_opt SEMICOLON bool_expression SEMICOLON expr_opt RPAREN statement 
| DO statement WHILE LPAREN bool_expression RPAREN SEMICOLON
;

expr_opt:  	
	| assign_expression
;

return_stmt:		  RETURN SEMICOLON
			| RETURN expression SEMICOLON
;

bool_expression: expression 
;

assign_expression:
  lvalue_location ASSIGN expression
| expression
;

expression:
  unary_expression
| expression BITWISE_OR expression
| expression BITWISE_XOR expression
| expression AMPERSAND expression
| expression EQ expression
| expression NEQ expression
| expression LT expression
| expression GT expression
| expression LTE expression
| expression GTE expression
| expression LSHIFT expression
| expression RSHIFT expression
| expression PLUS expression
| expression MINUS expression
| expression STAR expression
| expression DIV expression
| expression MOD expression
| BOOL LPAREN expression RPAREN
| I2P LPAREN expression RPAREN
| P2I LPAREN expression RPAREN
| ZEXT LPAREN expression RPAREN
| SEXT LPAREN expression RPAREN
| ID LPAREN argument_list_opt RPAREN
| LPAREN expression RPAREN
;


argument_list_opt: | argument_list
;

argument_list:
  expression
| argument_list COMMA expression
;


unary_expression:         primary_expression
| AMPERSAND primary_expression
| STAR primary_expression
| MINUS unary_expression
| PLUS unary_expression
| BITWISE_INVERT unary_expression
;

primary_expression:
  lvalue_location
| constant
;

lvalue_location:
  ID
| lvalue_location LBRACKET expression RBRACKET
| STAR LPAREN expression RPAREN
;

constant_expression:
  unary_constant_expression
| constant_expression BITWISE_OR constant_expression
| constant_expression BITWISE_XOR constant_expression
| constant_expression AMPERSAND constant_expression
| constant_expression LSHIFT constant_expression
| constant_expression RSHIFT constant_expression
| constant_expression PLUS constant_expression
| constant_expression MINUS constant_expression
| constant_expression STAR constant_expression
| constant_expression DIV constant_expression
| constant_expression MOD constant_expression
| I2P LPAREN constant_expression RPAREN
| LPAREN constant_expression RPAREN
;

unary_constant_expression:
  constant
| MINUS unary_constant_expression
| PLUS unary_constant_expression
| BITWISE_INVERT unary_constant_expression
;


constant:	          CONSTANT_INTEGER
{
  $$ = Builder->getInt64($1);
}
;


%%

Value* BuildFunction(Type* RetType, const char *name, 
			   parameter_list *params)
{
  std::vector<Type*> v;
  std::vector<const char*> vname;

  if (params)
    for(auto ii : *params)
      {
	vname.push_back( ii.second );
	v.push_back( ii.first );      
      }
  
  ArrayRef<Type*> Params(v);

  FunctionType* FunType = FunctionType::get(RetType,Params,false);

  Fun = Function::Create(FunType,GlobalValue::ExternalLinkage,
			 name,M);
  Twine T("entry");
  BasicBlock *BB = BasicBlock::Create(M->getContext(),T,Fun);

  /* Create an Instruction Builder */
  Builder = new IRBuilder<>(M->getContext());
  Builder->SetInsertPoint(BB);

  Function::arg_iterator I = Fun->arg_begin();
  for(int i=0; I!=Fun->arg_end();i++, I++)
    {
      // map args and create allocas!
      AllocaInst *AI = Builder->CreateAlloca(v[i]);
      Builder->CreateStore(&(*I),(Value*)AI);
      symbol_insert(vname[i],(Value*)AI);
    }


  return Fun;
}

extern int verbose;
extern int line_num;
extern char *infile[];
static int   infile_cnt=0;
extern FILE * yyin;
extern int use_stdin;

int parser_error(const char *msg)
{
  if (use_stdin)
    printf("stdin:%d: Error -- %s\n",line_num,msg);
  else
    printf("%s:%d: Error -- %s\n",infile[infile_cnt-1],line_num,msg);
  return 1;
}

int internal_error(const char *msg)
{
  printf("%s:%d Internal Error -- %s\n",infile[infile_cnt-1],line_num,msg);
  return 1;
}

int yywrap() {

  if (use_stdin)
    {
      yyin = stdin;
      return 0;
    }
  
  static FILE * currentFile = NULL;

  if ( (currentFile != 0) ) {
    fclose(yyin);
  }
  
  if(infile[infile_cnt]==NULL)
    return 1;

  currentFile = fopen(infile[infile_cnt],"r");
  if(currentFile!=NULL)
    yyin = currentFile;
  else
    printf("Could not open file: %s",infile[infile_cnt]);

  infile_cnt++;
  
  return (currentFile)?0:1;
}

int yyerror(const char* error)
{
  parser_error("Un-resolved syntax error.");
  return 1;
}

char * get_filename()
{
  return infile[infile_cnt-1];
}

int get_lineno()
{
  return line_num;
}


void cmm_abort()
{
  parser_error("Too many errors to continue.");
  exit(1);
}
