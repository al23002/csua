%code top {
#include <stdint.h>

#define YY_YY_Y_TAB_H_INCLUDED

/* このパーサファイル内だけで差し替え（警告が出る処理系もあります） */
#ifdef __INT_LEAST8_TYPE__
# undef __INT_LEAST8_TYPE__
# define __INT_LEAST8_TYPE__  int8_t
#endif
#ifdef __UINT_LEAST8_TYPE__
# undef __UINT_LEAST8_TYPE__
# define __UINT_LEAST8_TYPE__ uint8_t
#endif
#ifdef __INT_LEAST16_TYPE__
# undef __INT_LEAST16_TYPE__
# define __INT_LEAST16_TYPE__  int16_t
#endif
#ifdef __UINT_LEAST16_TYPE__
# undef __UINT_LEAST16_TYPE__
# define __UINT_LEAST16_TYPE__ uint16_t
#endif
}

%{
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>

#define YYDEBUG 1

#include "ast.h"
#include "definitions.h"
#include "compiler.h"
#include "create.h"
#include "scanner.h"
#include "cminor_type.h"
#include "parsed_type.h"
#include "header_store.h"

int yyerror(YYLTYPE *yylloc_param, Scanner *scanner, char const *str);

/* Shorthand macro for getting creator from scanner */
#define C cs_scanner_get_creator(scanner)

static ParsedType *attach_declarator(ParsedType *base,
                                     DeclaratorInfo declarator)
{
    ParsedType *full_type = base;
    if (declarator.type)
    {
        ParsedType *tail = declarator.type;
        while (tail->child)
            tail = tail->child;
        tail->child = base;
        full_type = declarator.type;
    }
    return full_type;
}

static bool parameter_list_has_ellipsis(ParameterList *list)
{
    for (; list; list = list->next)
    {
        if (list->is_ellipsis)
        {
            return true;
        }
    }
    return false;
}

static DeclaratorInfoNode *create_declarator_node(DeclaratorInfo declarator)
{
    DeclaratorInfoNode *node = (DeclaratorInfoNode *)calloc(1, sizeof(DeclaratorInfoNode));
    node->info = declarator;
    node->next = NULL;
    return node;
}

static DeclaratorInfoNode *append_declarator_node(DeclaratorInfoNode *list,
                                                  DeclaratorInfo declarator)
{
    DeclaratorInfoNode *node = create_declarator_node(declarator);
    if (list == NULL)
    {
        return node;
    }
    DeclaratorInfoNode *tail = list;
    while (tail->next)
    {
        tail = tail->next;
    }
    tail->next = node;
    return list;
}

static ParsedType *resolve_typedef(char *name)
{
    /* Always defer typedef resolution to semantic analysis. */
    return cs_parsed_type_named(CS_TYPEDEF_NAME, name);
}

/* Extract integer value from a constant expression (for enum initializers) */
static int get_const_int_value(Expression *expr)
{
    if (!expr) return 0;

    /* Integer literal (signed/unsigned, int/long) */
    if (expr->kind == INT_EXPRESSION || expr->kind == UINT_EXPRESSION)
    {
        return expr->u.int_value;
    }
    if (expr->kind == LONG_EXPRESSION || expr->kind == ULONG_EXPRESSION)
    {
        return (int)expr->u.long_value;
    }

    /* Unary minus on integer literal */
    if (expr->kind == MINUS_EXPRESSION && expr->u.minus_expression)
    {
        Expression *operand = expr->u.minus_expression;
        if (operand->kind == INT_EXPRESSION || operand->kind == UINT_EXPRESSION)
        {
            return -operand->u.int_value;
        }
        if (operand->kind == LONG_EXPRESSION || operand->kind == ULONG_EXPRESSION)
        {
            return -(int)operand->u.long_value;
        }
    }

    fprintf(stderr, "enum initializer must be an integer constant\n");
    return 0;
}

/* Create an EnumMember node with explicit value */
static EnumMember *create_enum_member_with_value(char *name, int value)
{
    EnumMember *member = (EnumMember *)calloc(1, sizeof(EnumMember));
    member->name = name;
    member->value = value;
    member->has_explicit_value = true;
    member->enum_def = NULL;
    member->next = NULL;
    return member;
}

/* Create an EnumMember node (value assigned later when registering) */
static EnumMember *create_enum_member(char *name)
{
    EnumMember *member = (EnumMember *)calloc(1, sizeof(EnumMember));
    member->name = name;
    member->value = 0;
    member->has_explicit_value = false;
    member->enum_def = NULL;
    member->next = NULL;
    return member;
}

/* Append an EnumMember to the end of a list */
static EnumMember *append_enum_member(EnumMember *list, EnumMember *member)
{
    if (list == NULL)
    {
        return member;
    }
    EnumMember *tail = list;
    while (tail->next)
    {
        tail = tail->next;
    }
    tail->next = member;
    return list;
}

/* Modifier constants removed - unsigned is now part of base_type_def,
 * const is a type prefix, static/extern handled at declaration level */

/* ★これを追加：Bison内蔵の動的拡張コードを抑止 */
#define yyoverflow(...)            \
    do {                            \
        goto yyexhaustedlab;          \
    } while (0)

%}
%define api.pure full
%parse-param {Scanner *scanner}
%lex-param {Scanner *scanner}
%locations
%expect 6

%code requires {
typedef struct DeclaratorInfo_tag
{
    char *name;
    ParsedType *type;
    ParameterList *parameters;
    AttributeSpecifier *attributes;
    bool is_function; /* true if declarator has () */
} DeclaratorInfo;

typedef struct DeclaratorInfoNode_tag
{
    DeclaratorInfo info;
    struct DeclaratorInfoNode_tag *next;
} DeclaratorInfoNode;

/* YYLTYPE is defined in scanner.h */
#define YYLTYPE_IS_DECLARED

#ifndef YYLLOC_DEFAULT
#define YYLLOC_DEFAULT(Current, Rhs, N)                                      \
    do                                                                       \
    {                                                                        \
        if (N)                                                               \
        {                                                                    \
            (Current).first_line = YYRHSLOC(Rhs, 1).first_line;               \
            (Current).first_column = YYRHSLOC(Rhs, 1).first_column;           \
            (Current).last_line = YYRHSLOC(Rhs, N).last_line;                 \
            (Current).last_column = YYRHSLOC(Rhs, N).last_column;             \
            (Current).filename = YYRHSLOC(Rhs, N).filename;                   \
        }                                                                    \
        else                                                                 \
        {                                                                    \
            (Current).first_line = (Current).last_line = YYRHSLOC(Rhs, 0).last_line; \
            (Current).first_column = (Current).last_column = YYRHSLOC(Rhs, 0).last_column; \
            (Current).filename = YYRHSLOC(Rhs, 0).filename;                   \
        }                                                                    \
    } while (0)
#endif
}

%union{
    int                  iv;
    long                 lv;
    double               dv;
    float                fv;
    char                *name;
    CS_String            str;
    Expression          *expression;
    ExpressionList      *expression_list;
    Statement           *statement;
    StatementList       *statement_list;
    FunctionDeclaration *function_declaration;
    AssignmentOperator   assignment_operator;
    TypeSpecifier       *type_specifier;
    ParsedType          *parsed_type;
    ParameterList       *parameter_list;
    ArgumentList        *argument_list;
    AttributeSpecifier  *attribute;
    DeclaratorInfo       declarator;
    StructMember        *struct_member;
    EnumMember          *enum_member;
    DeclaratorInfoNode *declarator_list;
}

%token LP
%token RP
%token LC
%token RC
%token COMMA
%token LBRACKET
%token RBRACKET
%token <name> ATTRIBUTE
%token LOGICAL_AND
%token LOGICAL_OR
%token BIT_AND
%token BIT_OR
%token BIT_XOR
%token EQ
%token ASSIGN_T
%token NE
%token GT
%token GE
%token LE
%token LT
%token SEMICOLON
%token COLON
%token QUESTION
%token ADD
%token SUB
%token MUL
%token DIV
%token MOD
%token ADD_ASSIGN_T
%token SUB_ASSIGN_T
%token MUL_ASSIGN_T
%token DIV_ASSIGN_T
%token MOD_ASSIGN_T
%token INCREMENT
%token DECREMENT
%token EXCLAMATION
%token DOT

/* New tokens for C compatibility */
%token ARROW
%token LSHIFT
%token RSHIFT
%token TILDE
%token ELLIPSIS
%token AND_ASSIGN_T
%token OR_ASSIGN_T
%token XOR_ASSIGN_T
%token LSHIFT_ASSIGN_T
%token RSHIFT_ASSIGN_T

%token <iv>   INT_LITERAL
%token <iv>   UINT_LITERAL
%token <lv>   LONG_LITERAL
%token <lv>   ULONG_LITERAL
%token <dv>   DOUBLE_LITERAL
%token <fv>   FLOAT_LITERAL
%token <name> IDENTIFIER
/* TYPE_NAME token removed - position-based disambiguation eliminates scanner hack */
%token <str> STRING_LITERAL


%token IF
%token ELSE
%token ELSIF
%token WHILE
%token DO
%token FOR
%token RETURN
%token BREAK
%token CONTINUE
%token INT_T
%token DOUBLE_T
%token STRING_T

/* C type keywords */
%token VOID_T
%token CHAR_T
%token BOOL_T
%token SHORT_T
%token LONG_T
%token UNSIGNED_T
%token FLOAT_T

/* Boolean literal keywords */
%token TRUE_T
%token FALSE_T

/* Null pointer keyword */
%token NULL_T

/* Storage class / type qualifiers */
%token STATIC_T
%token CONST_T
%token EXTERN_T
%token TYPEDEF_T

/* Composite type keywords */
%token STRUCT_T
%token UNION_T
%token ENUM_T

/* Control flow keywords */
%token SWITCH
%token CASE
%token DEFAULT
%token GOTO

/* Other keywords */
%token SIZEOF

%nonassoc LOWER_THAN_ELSE
%nonassoc ELSE
%nonassoc ELSIF

%type <expression> expression assignment_expression conditional_expression logical_or_expression
                 logical_and_expression equality_expression relational_expression
                 bitwise_or_expression bitwise_xor_expression bitwise_and_expression
                 shift_expression additive_expression multiplicative_expression unary_expression
                 postfix_expression primary_expression initializer
                 side_effect_expression

%type <expression_list> initializer_list

%type <assignment_operator> assignment_operator
%type <parsed_type> type_ref base_type_def
%type <statement> statement declaration_statement selection_statement
                 iteration_statement jump_statement labeled_statement
                 else_clause compound_item
%type <function_declaration> function_definition
%type <parameter_list> parameter_list
%type <argument_list> argument_list
%type <statement> compound_statement
%type <statement_list> compound_item_list
%type <declarator> declarator direct_declarator
%type <attribute> attribute_specifier_seq
%type <parsed_type> array_suffix
%type <iv> pointer_opt pointer
%type <expression> expression_opt cast_expression safe_unary_expression
%type <parameter_list> parameter_list_opt
%type <parameter_list> parameter_declaration
%type <struct_member> struct_body struct_declaration
%type <declarator_list> struct_declarator_list
%type <enum_member> enum_body enumerator_list

%%
translation_unit
    : /* empty */
    | translation_unit definition_or_statement
    ;

definition_or_statement
        : function_definition
        {
           CS_Compiler* compiler = cs_scanner_get_tu(scanner);
           if (compiler && compiler->current_file_decl) {
               header_decl_add_function(compiler->current_file_decl, $1);
           }
        }
        | type_ref declarator SEMICOLON
        {
           CS_Compiler* compiler = cs_scanner_get_tu(scanner);
           if (compiler) {
               ParsedType *full_type = attach_declarator($1, $2);
               if ($2.is_function) {
                   /* Function prototype: add directly to FileDecl */
                   bool is_variadic = parameter_list_has_ellipsis($2.parameters);
                   FunctionDeclaration *func = cs_create_function_declaration(C, full_type, $2.name, $2.parameters, is_variadic, false, $2.attributes, NULL);
                   if (compiler->current_file_decl) {
                       header_decl_add_function(compiler->current_file_decl, func);
                   }
               } else {
                   Statement *stmt = cs_create_declaration_statement(C, full_type, $2.name, NULL, false);
                   compiler->stmt_list = cs_chain_statement_list(compiler->stmt_list, stmt);
               }
           }
        }
        | type_ref declarator ASSIGN_T initializer SEMICOLON
        {
           CS_Compiler* compiler = cs_scanner_get_tu(scanner);
           if (compiler) {
               ParsedType *full_type = attach_declarator($1, $2);
               Statement *stmt = cs_create_declaration_statement(C, full_type, $2.name, $4, false);
               compiler->stmt_list = cs_chain_statement_list(compiler->stmt_list, stmt);
           }
        }
        /* Static variable declarations (static const int x = 0;) */
        | STATIC_T type_ref declarator SEMICOLON
        {
           CS_Compiler* compiler = cs_scanner_get_tu(scanner);
           if (compiler) {
               ParsedType *full_type = attach_declarator($2, $3);
               if ($3.is_function) {
                   /* Static function prototype: add directly to FileDecl */
                   bool is_variadic = parameter_list_has_ellipsis($3.parameters);
                   FunctionDeclaration *func = cs_create_function_declaration(C, full_type, $3.name, $3.parameters, is_variadic, true, $3.attributes, NULL);
                   if (compiler->current_file_decl) {
                       header_decl_add_function(compiler->current_file_decl, func);
                   }
               } else {
                   Statement *stmt = cs_create_declaration_statement(C, full_type, $3.name, NULL, true);
                   compiler->stmt_list = cs_chain_statement_list(compiler->stmt_list, stmt);
               }
           }
        }
        | STATIC_T type_ref declarator ASSIGN_T initializer SEMICOLON
        {
           CS_Compiler* compiler = cs_scanner_get_tu(scanner);
           if (compiler) {
               ParsedType *full_type = attach_declarator($2, $3);
               Statement *stmt = cs_create_declaration_statement(C, full_type, $3.name, $5, true);
               compiler->stmt_list = cs_chain_statement_list(compiler->stmt_list, stmt);
           }
        }
        /* Extern declarations (extern int foo; extern void bar();) */
        | EXTERN_T type_ref declarator SEMICOLON
        {
           CS_Compiler* compiler = cs_scanner_get_tu(scanner);
           if (compiler) {
               ParsedType *full_type = attach_declarator($2, $3);
               if ($3.is_function) {
                   /* Extern function prototype: add directly to FileDecl */
                   bool is_variadic = parameter_list_has_ellipsis($3.parameters);
                   FunctionDeclaration *func = cs_create_function_declaration(C, full_type, $3.name, $3.parameters, is_variadic, false, $3.attributes, NULL);
                   if (compiler->current_file_decl) {
                       header_decl_add_function(compiler->current_file_decl, func);
                   }
               } else {
                   Statement *stmt = cs_create_declaration_statement(C, full_type, $3.name, NULL, false);
                   /* Mark as extern - no field generation, just reference */
                   stmt->u.declaration_s->is_extern = true;
                   compiler->stmt_list = cs_chain_statement_list(compiler->stmt_list, stmt);
                   /* Also add extern variable to FileDecl for cross-file lookup */
                   if (compiler->current_file_decl) {
                       header_decl_add_declaration(compiler->current_file_decl, stmt->u.declaration_s);
                   }
               }
           }
        }
        | struct_union_enum_definition
        ;

function_definition
        : type_ref declarator compound_statement
        {
            ParsedType *full_type = attach_declarator($1, $2);
            bool is_variadic = parameter_list_has_ellipsis($2.parameters);
            $$ = cs_create_function_declaration(C, full_type, $2.name, $2.parameters,
                                                is_variadic, false, $2.attributes, $3);
        }
        /* Static function: private in JVM */
        | STATIC_T type_ref declarator compound_statement
        {
            ParsedType *full_type = attach_declarator($2, $3);
            bool is_variadic = parameter_list_has_ellipsis($3.parameters);
            $$ = cs_create_function_declaration(C, full_type, $3.name, $3.parameters,
                                                is_variadic, true, $3.attributes, $4);
        }
        ;

struct_union_enum_definition
        : STRUCT_T IDENTIFIER LC struct_body RC SEMICOLON
        {
            cs_register_struct_definition(cs_scanner_get_tu(scanner), $2, $4, false);
        }
        | STRUCT_T LC struct_body RC SEMICOLON
        | UNION_T IDENTIFIER LC struct_body RC SEMICOLON
        {
            cs_register_struct_definition(cs_scanner_get_tu(scanner), $2, $4, true);
        }
        | UNION_T LC struct_body RC SEMICOLON
        | ENUM_T IDENTIFIER LC enum_body RC SEMICOLON
        {
            cs_register_enum_definition(cs_scanner_get_tu(scanner), $2, $4);
        }
        | ENUM_T LC enum_body RC SEMICOLON
        {
            cs_register_enum_definition(cs_scanner_get_tu(scanner), NULL, $3);
        }
        /* typedef type_ref pointer_opt IDENTIFIER;
         * Handles: typedef struct Foo { ... } Bar;
         *          typedef struct Foo Bar;
         *          typedef int MyInt;
         *          typedef int* IntPtr;  (pointer typedef)
         *          typedef Foo Bar; (typedef of typedef - type_ref includes IDENTIFIER)
         *          typedef struct { ... } Foo; (anonymous - register with typedef name)
         * Note: type_ref includes both built-in types and typedef names (IDENTIFIER). */
        | TYPEDEF_T type_ref pointer_opt IDENTIFIER SEMICOLON
        {
            CS_Compiler *compiler = cs_scanner_get_tu(scanner);
            const char *type_name = $2->name;
            CS_BasicType basic = $2->basic_type;
            ParsedType *parsed = $2;
            int pointer_level = $3;

            /* Wrap with pointer if pointer_level > 0 */
            if (pointer_level > 0) {
                parsed = cs_wrap_parsed_pointer(parsed, pointer_level);
            }

            /* Add typedef to current file (type resolution happens in semantic phase) */
            TypedefDefinition *tdef = (TypedefDefinition *)calloc(1, sizeof(TypedefDefinition));
            tdef->name = strdup($4);
            tdef->parsed_type = cs_copy_parsed_type(parsed);
            tdef->type = NULL;       /* Set during semantic analysis */
            tdef->canonical = NULL;  /* Set during semantic analysis */
            tdef->source_path = compiler->current_file_decl ? strdup(compiler->current_file_decl->path) : NULL;
            tdef->next = NULL;
            if (compiler->current_file_decl) {
                header_decl_add_typedef(compiler->current_file_decl, tdef);
            }
        }
        ;

struct_body
        : /* empty */
        {
            $$ = NULL;
        }
        | struct_body struct_declaration
        {
            $$ = cs_chain_struct_member($1, $2);
        }
        ;

struct_declaration
        : type_ref struct_declarator_list SEMICOLON
        {
            $$ = NULL;
            for (DeclaratorInfoNode *node = $2; node; node = node->next)
            {
                ParsedType *full_parsed = attach_declarator($1, node->info);
                StructMember *member = cs_create_struct_member(full_parsed, node->info.name);
                $$ = cs_chain_struct_member($$, member);
            }
        }
        | SEMICOLON
        {
            $$ = NULL;
        }
        ;

struct_declarator_list
        : declarator
        {
            $$ = create_declarator_node($1);
        }
        | struct_declarator_list COMMA declarator
        {
            $$ = append_declarator_node($1, $3);
        }
        ;

enum_body
        : enumerator_list
        { $$ = $1; }
        | enumerator_list COMMA
        { $$ = $1; }
        | /* empty */
        { $$ = NULL; }
        ;

enumerator_list
        : IDENTIFIER
        { $$ = create_enum_member($1); }
        | IDENTIFIER ASSIGN_T assignment_expression
        { $$ = create_enum_member_with_value($1, get_const_int_value($3)); }
        | enumerator_list COMMA IDENTIFIER
        { $$ = append_enum_member($1, create_enum_member($3)); }
        | enumerator_list COMMA IDENTIFIER ASSIGN_T assignment_expression
        { $$ = append_enum_member($1, create_enum_member_with_value($3, get_const_int_value($5))); }
        ;


parameter_list
        : parameter_declaration
        { $$ = $1; }
        | parameter_list COMMA parameter_declaration
        { $$ = cs_chain_parameter_list($1, $3); }
        | parameter_list COMMA ELLIPSIS
        { $$ = cs_chain_parameter_list($1, cs_create_parameter(C, NULL, NULL, true)); }
        | ELLIPSIS
        { $$ = cs_create_parameter(C, NULL, NULL, true); }
        ;

parameter_declaration
        : type_ref declarator
        {
            ParsedType *full = attach_declarator($1, $2);
            $$ = cs_create_parameter(C, full, $2.name, false);
        }
        ;

parameter_list_opt
        : /* empty */
        { $$ = NULL; }
        | parameter_list
        { $$ = $1; }
        ;

argument_list
        : assignment_expression { $$ = cs_create_argument($1); }
        | argument_list COMMA assignment_expression { $$ = cs_chain_argument_list($1, $3); }


statement
        : side_effect_expression SEMICOLON
        {
            $$ = cs_create_expression_statement(C, $1);
        }
        | declaration_statement
        | compound_statement
        | selection_statement
        | iteration_statement
        | jump_statement
        | labeled_statement
        | SEMICOLON
        {
            $$ = NULL;
        }
        ;

compound_statement
        : LC RC
        {
            $$ = cs_create_compound_statement(C, NULL);
        }
        | LC compound_item_list RC
        {
            $$ = cs_create_compound_statement(C, $2);
        }
        ;

compound_item_list
        : compound_item
        { $$ = cs_create_statement_list($1); }
        | compound_item_list compound_item
        { $$ = cs_chain_statement_list($1, $2); }
        ;

compound_item
        : statement
        ;

selection_statement
        : IF LP expression RP statement %prec LOWER_THAN_ELSE
        {
            $$ = cs_create_if_statement(C, $3, $5, NULL);
        }
        | IF LP expression RP statement else_clause
        {
            $$ = cs_create_if_statement(C, $3, $5, $6);
        }
        | SWITCH LP expression RP statement
        {
            $$ = cs_create_switch_statement(C, $3, $5);
        }
        ;

else_clause
        : ELSE statement
        {
            $$ = $2;
        }
        | ELSIF LP expression RP statement else_clause
        {
            $$ = cs_create_if_statement(C, $3, $5, $6);
        }
        ;

iteration_statement
        : WHILE LP expression RP statement
        {
            $$ = cs_create_while_statement(C, $3, $5);
        }
        | DO statement WHILE LP expression RP SEMICOLON
        {
            $$ = cs_create_do_while_statement(C, $2, $5);
        }
        /* For loop without declaration - uses side_effect_expression to avoid
         * conflict with declaration (Foo * bar vs expression). */
        | FOR LP SEMICOLON expression_opt SEMICOLON expression_opt RP statement
        {
            $$ = cs_create_for_statement(C, NULL, $4, $6, $8);
        }
        | FOR LP side_effect_expression SEMICOLON expression_opt SEMICOLON expression_opt RP statement
        {
            $$ = cs_create_for_statement(C, cs_create_expression_statement(C, $3), $5, $7, $9);
        }
        | FOR LP declaration_statement expression_opt SEMICOLON expression_opt RP statement
        {
            $$ = cs_create_for_statement(C, $3, $4, $6, $8);
        }
        ;

expression_opt
        : /* empty */
        {
            $$ = NULL;
        }
        | expression
        {
            $$ = $1;
        }
        ;

jump_statement
        : BREAK SEMICOLON
        {
            $$ = cs_create_break_statement(C);
        }
        | CONTINUE SEMICOLON
        {
            $$ = cs_create_continue_statement(C);
        }
        | RETURN expression_opt SEMICOLON
        {
            $$ = cs_create_return_statement(C, $2);
        }
        | GOTO IDENTIFIER SEMICOLON
        {
            $$ = cs_create_goto_statement(C, $2);
        }
        ;

labeled_statement
        : IDENTIFIER COLON statement
        {
            $$ = cs_create_label_statement(C, $1, $3);
        }
        | CASE expression COLON statement
        {
            $$ = cs_create_case_statement(C, $2, $4);
        }
        | DEFAULT COLON statement
        {
            $$ = cs_create_default_statement(C, $3);
        }
        ;

/* Removed permissive skip_token/any_token rules to avoid ambiguity */

type_qualifier
        : CONST_T
        ;

type_qualifier_list
        : type_qualifier
        | type_qualifier_list type_qualifier
        ;

pointer_opt
        : /* empty */ { $$ = 0; }
        | pointer { $$ = $1; }
        ;

/* pointer: At least one MUL - used for typedef casts like (Foo *)x */
pointer
        : MUL { $$ = 1; }
        | pointer MUL { $$ = $1 + 1; }
        | MUL type_qualifier_list { $$ = 1; }
        | pointer MUL type_qualifier_list { $$ = $1 + 1; }
        ;

array_suffix
        : /* empty */ { $$ = NULL; }
        | array_suffix LBRACKET expression RBRACKET
        {
            ParsedType *arr = cs_wrap_parsed_array(NULL, $3);
            if ($1)
            {
                /* Append new dimension to the end of existing suffix chain.
                 * For int arr[2][3]: first [2] creates {size=2},
                 * then [3] appends {size=3} as child, giving {size=2, child={size=3}}.
                 * This matches C semantics: arr[2][3] is "array of 2 arrays of 3 ints". */
                ParsedType *tail = $1;
                while (tail->child)
                    tail = tail->child;
                tail->child = arr;
                $$ = $1;
            }
            else
            {
                $$ = arr;
            }
        }
        | array_suffix LBRACKET RBRACKET
        {
            ParsedType *arr = cs_wrap_parsed_array(NULL, NULL);
            if ($1)
            {
                ParsedType *tail = $1;
                while (tail->child)
                    tail = tail->child;
                tail->child = arr;
                $$ = $1;
            }
            else
            {
                $$ = arr;
            }
        }
        ;

declarator
        : pointer_opt direct_declarator
        {
            ParsedType *wrapper = NULL;
            if ($1 > 0)
                wrapper = cs_wrap_parsed_pointer(NULL, $1);
            if ($2.type)
            {
                if (wrapper)
                {
                    ParsedType *tail = $2.type;
                    while (tail->child)
                        tail = tail->child;
                    tail->child = wrapper;
                    wrapper = $2.type;
                }
                else
                {
                    wrapper = $2.type;
                }
            }
            $$.type = wrapper;
            $$.name = $2.name;
            $$.parameters = $2.parameters;
            $$.attributes = $2.attributes;
            $$.is_function = $2.is_function;
        }
        ;

direct_declarator
        : IDENTIFIER array_suffix
        {
            ParsedType *wrapper = NULL;
            if ($2)
            {
                wrapper = $2;
            }
            $$.type = wrapper;
            $$.name = $1;
            $$.parameters = NULL;
            $$.attributes = NULL;
            $$.is_function = false;
        }
        | direct_declarator LP parameter_list_opt RP
        {
            $$.type = $1.type;
            $$.name = $1.name;
            $$.parameters = $3;
            $$.attributes = $1.attributes;
            $$.is_function = true;
        }
        | direct_declarator attribute_specifier_seq LP parameter_list_opt RP
        {
            $$.type = $1.type;
            $$.name = $1.name;
            $$.parameters = $4;
            $$.attributes = cs_chain_attribute($1.attributes, $2);
            $$.is_function = true;
        }
        ;

attribute_specifier_seq
        : ATTRIBUTE           { $$ = cs_create_attribute($1); }
        | attribute_specifier_seq ATTRIBUTE
        { $$ = cs_chain_attribute($1, cs_create_attribute($2)); }
        ;

declaration_statement
        : type_ref declarator SEMICOLON
        {
            ParsedType *full_type = attach_declarator($1, $2);
            $$ = cs_create_declaration_statement(C, full_type, $2.name, NULL, false);
        }
        | type_ref declarator ASSIGN_T initializer SEMICOLON
        {
            ParsedType *full_type = attach_declarator($1, $2);
            $$ = cs_create_declaration_statement(C, full_type, $2.name, $4, false);
        }
        /* Static local variable (treated as regular local in JVM) */
        | STATIC_T type_ref declarator SEMICOLON
        {
            ParsedType *full_type = attach_declarator($2, $3);
            $$ = cs_create_declaration_statement(C, full_type, $3.name, NULL, true);
        }
        | STATIC_T type_ref declarator ASSIGN_T initializer SEMICOLON
        {
            ParsedType *full_type = attach_declarator($2, $3);
            $$ = cs_create_declaration_statement(C, full_type, $3.name, $5, true);
        }
        ;


/* type_ref: Unified type reference for declarations.
 * - CONST_T prefix marks const types (const int, const Foo)
 * - UNSIGNED_T is integrated into base_type_def
 * - IDENTIFIER handles typedef names
 * Position-based disambiguation: IDENTIFIER is a type only in type positions. */
type_ref
        : base_type_def
        | CONST_T base_type_def
        {
            cs_parsed_type_set_const($2, true);
            $$ = $2;
        }
        | base_type_def CONST_T
        {
            cs_parsed_type_set_const($1, true);
            $$ = $1;
        }
        | IDENTIFIER
        {
            $$ = resolve_typedef($1);
        }
        | CONST_T IDENTIFIER
        {
            ParsedType *pt = resolve_typedef($2);
            cs_parsed_type_set_const(pt, true);
            $$ = pt;
        }
        | IDENTIFIER CONST_T
        {
            ParsedType *pt = resolve_typedef($1);
            cs_parsed_type_set_const(pt, true);
            $$ = pt;
        }
        ;

/* base_type_def: Primitive types (with unsigned variants) and composite types.
 * IDENTIFIER (typedef names) excluded - handled separately in type_ref. */
base_type_def
        : INT_T     { $$ = cs_parsed_type_basic(CS_INT_TYPE); }
        | UNSIGNED_T INT_T
        {
            ParsedType *pt = cs_parsed_type_basic(CS_INT_TYPE);
            cs_parsed_type_set_unsigned(pt, true);
            $$ = pt;
        }
        | UNSIGNED_T
        {
            /* unsigned alone = unsigned int */
            ParsedType *pt = cs_parsed_type_basic(CS_INT_TYPE);
            cs_parsed_type_set_unsigned(pt, true);
            $$ = pt;
        }
        | LONG_T    { $$ = cs_parsed_type_basic(CS_LONG_TYPE); }
        | UNSIGNED_T LONG_T
        {
            ParsedType *pt = cs_parsed_type_basic(CS_LONG_TYPE);
            cs_parsed_type_set_unsigned(pt, true);
            $$ = pt;
        }
        | SHORT_T   { $$ = cs_parsed_type_basic(CS_SHORT_TYPE); }
        | UNSIGNED_T SHORT_T
        {
            ParsedType *pt = cs_parsed_type_basic(CS_SHORT_TYPE);
            cs_parsed_type_set_unsigned(pt, true);
            $$ = pt;
        }
        | CHAR_T    { $$ = cs_parsed_type_basic(CS_CHAR_TYPE); }
        | UNSIGNED_T CHAR_T
        {
            ParsedType *pt = cs_parsed_type_basic(CS_CHAR_TYPE);
            cs_parsed_type_set_unsigned(pt, true);
            $$ = pt;
        }
        | FLOAT_T   { $$ = cs_parsed_type_basic(CS_FLOAT_TYPE); }
        | DOUBLE_T  { $$ = cs_parsed_type_basic(CS_DOUBLE_TYPE); }
        | VOID_T    { $$ = cs_parsed_type_basic(CS_VOID_TYPE); }
        | BOOL_T    { $$ = cs_parsed_type_basic(CS_BOOLEAN_TYPE); }
        | STRUCT_T IDENTIFIER
        {
            $$ = cs_parsed_type_named(CS_STRUCT_TYPE, $2);
        }
        | UNION_T  IDENTIFIER { $$ = cs_parsed_type_named(CS_UNION_TYPE, $2); }
        | ENUM_T   IDENTIFIER { $$ = cs_parsed_type_named(CS_ENUM_TYPE, $2); }
        | STRUCT_T IDENTIFIER LC struct_body RC
        {
            CS_Compiler *compiler = cs_scanner_get_tu(scanner);
            cs_register_struct_definition(compiler, $2, $4, false);
            $$ = cs_parsed_type_named(CS_STRUCT_TYPE, $2);
        }
        | UNION_T  IDENTIFIER LC struct_body RC
        {
            CS_Compiler *compiler = cs_scanner_get_tu(scanner);
            cs_register_struct_definition(compiler, $2, $4, true);
            $$ = cs_parsed_type_named(CS_UNION_TYPE, $2);
        }
        | ENUM_T   IDENTIFIER LC enum_body RC
        {
            CS_Compiler *compiler = cs_scanner_get_tu(scanner);
            cs_register_enum_definition(compiler, $2, $4);
            $$ = cs_parsed_type_named(CS_ENUM_TYPE, $2);
        }
        | STRUCT_T LC struct_body RC
        {
            CS_Compiler *compiler = cs_scanner_get_tu(scanner);
            cs_register_struct_definition(compiler, NULL, $3, false);
            /* Use generated name from last_anon_struct_def */
            char *anon_name = compiler->last_anon_struct_def ? strdup(compiler->last_anon_struct_def->id.name) : NULL;
            $$ = cs_parsed_type_named(CS_STRUCT_TYPE, anon_name);
        }
        | UNION_T  LC struct_body RC
        {
            CS_Compiler *compiler = cs_scanner_get_tu(scanner);
            cs_register_struct_definition(compiler, NULL, $3, true);
            /* Use generated name from last_anon_struct_def */
            char *anon_name = compiler->last_anon_struct_def ? strdup(compiler->last_anon_struct_def->id.name) : NULL;
            $$ = cs_parsed_type_named(CS_UNION_TYPE, anon_name);
        }
        | ENUM_T   LC enum_body RC
        {
            CS_Compiler *compiler = cs_scanner_get_tu(scanner);
            cs_register_enum_definition(compiler, NULL, $3);
            /* Use generated name from last_anon_enum_def */
            char *anon_name = compiler->last_anon_enum_def ? strdup(compiler->last_anon_enum_def->id.name) : NULL;
            $$ = cs_parsed_type_named(CS_ENUM_TYPE, anon_name);
        }
        ;

/* type_name rule removed - sizeof and cast now use type_ref pointer_opt directly */

initializer
    : assignment_expression
    {
        $$ = $1;
    }
    | DOT IDENTIFIER ASSIGN_T assignment_expression
    {
        $$ = cs_create_designated_initializer_expression(C, $2, $4);
    }
    | LC initializer_list RC
    {
        $$ = cs_create_initializer_list_expression(C, $2);
    }
    | LC initializer_list COMMA RC
    {
        $$ = cs_create_initializer_list_expression(C, $2);
    }
    | LC RC
    {
        /* C23 empty initializer for zero initialization */
        $$ = cs_create_initializer_list_expression(C, NULL);
    }
    ;

initializer_list
    : initializer
    {
        $$ = cs_chain_expression_list(NULL, $1);
    }
    | initializer_list COMMA initializer
    {
        $$ = cs_chain_expression_list($1, $3);
    }
    ;

/* Side-effect expressions only - no pure arithmetic/comparison allowed as statements */
side_effect_expression
        : unary_expression assignment_operator assignment_expression
        {
            $$ = cs_create_assignment_expression(C, $1, $2, $3);
        }
        | INCREMENT unary_expression
        {
            $$ = cs_create_inc_dec_expression(C, $2, INCREMENT_EXPRESSION, true);
        }
        | DECREMENT unary_expression
        {
            $$ = cs_create_inc_dec_expression(C, $2, DECREMENT_EXPRESSION, true);
        }
        | postfix_expression INCREMENT
        {
            $$ = cs_create_inc_dec_expression(C, $1, INCREMENT_EXPRESSION, false);
        }
        | postfix_expression DECREMENT
        {
            $$ = cs_create_inc_dec_expression(C, $1, DECREMENT_EXPRESSION, false);
        }
        | postfix_expression LP RP
        {
            $$ = cs_create_function_call_expression(C, $1, NULL);
        }
        | postfix_expression LP argument_list RP
        {
            $$ = cs_create_function_call_expression(C, $1, $3);
        }
        | side_effect_expression COMMA assignment_expression
        {
            $$ = cs_create_comma_expression(C, $1, $3);
        }
        /* (void)expr; for suppressing warnings */
        | LP VOID_T RP cast_expression
        {
            ParsedType *void_type = cs_parsed_type_basic(CS_VOID_TYPE);
            $$ = cs_create_type_cast_expression(C, void_type, $4);
        }
        ;

expression
	: assignment_expression
         {
             Expression* expr = $1;
//             printf("type = %d\n", expr->kind);
             $$ = $1;
         }
    | expression COMMA assignment_expression
         {
             $$ = cs_create_comma_expression(C, $1, $3);
         }
	;


assignment_expression
        : conditional_expression
        | unary_expression assignment_operator assignment_expression
        {
          $$ = cs_create_assignment_expression(C, $1, $2, $3);
        }
        ;

conditional_expression
        : logical_or_expression
        | logical_or_expression QUESTION expression COLON conditional_expression
        {
            $$ = cs_create_conditional_expression(C, $1, $3, $5);
        }
        ;
assignment_operator
        : ASSIGN_T        { $$ = ASSIGN;     }
        | ADD_ASSIGN_T    { $$ = ADD_ASSIGN; }
        | SUB_ASSIGN_T    { $$ = SUB_ASSIGN; }
        | MUL_ASSIGN_T    { $$ = MUL_ASSIGN; }
        | DIV_ASSIGN_T    { $$ = DIV_ASSIGN; }
        | MOD_ASSIGN_T    { $$ = MOD_ASSIGN; }
        | AND_ASSIGN_T    { $$ = AND_ASSIGN; }
        | OR_ASSIGN_T     { $$ = OR_ASSIGN; }
        | XOR_ASSIGN_T    { $$ = XOR_ASSIGN; }
        | LSHIFT_ASSIGN_T { $$ = LSHIFT_ASSIGN; }
        | RSHIFT_ASSIGN_T { $$ = RSHIFT_ASSIGN; }
        ;

logical_or_expression
        : logical_and_expression
        | logical_or_expression LOGICAL_OR logical_and_expression { $$ = cs_create_binary_expression(C, LOGICAL_OR_EXPRESSION, $1, $3);  }
        ;
logical_and_expression
        : bitwise_or_expression
        | logical_and_expression LOGICAL_AND bitwise_or_expression  { $$ = cs_create_binary_expression(C, LOGICAL_AND_EXPRESSION, $1, $3);  }
        ;

bitwise_or_expression
        : bitwise_xor_expression
        | bitwise_or_expression BIT_OR bitwise_xor_expression  { $$ = cs_create_binary_expression(C, BIT_OR_EXPRESSION, $1, $3); }
        ;

bitwise_xor_expression
        : bitwise_and_expression
        | bitwise_xor_expression BIT_XOR bitwise_and_expression { $$ = cs_create_binary_expression(C, BIT_XOR_EXPRESSION, $1, $3); }
        ;

bitwise_and_expression
        : equality_expression
        | bitwise_and_expression BIT_AND equality_expression { $$ = cs_create_binary_expression(C, BIT_AND_EXPRESSION, $1, $3);}
        ;

equality_expression
        : relational_expression
        | equality_expression EQ relational_expression { $$ = cs_create_binary_expression(C, EQ_EXPRESSION, $1, $3);  }
        | equality_expression NE relational_expression { $$ = cs_create_binary_expression(C, NE_EXPRESSION, $1, $3);  }
        ;

relational_expression
        : shift_expression
        | relational_expression GT shift_expression { $$ = cs_create_binary_expression(C, GT_EXPRESSION, $1, $3); }
        | relational_expression GE shift_expression { $$ = cs_create_binary_expression(C, GE_EXPRESSION, $1, $3); }
        | relational_expression LT shift_expression { $$ = cs_create_binary_expression(C, LT_EXPRESSION, $1, $3); }
        | relational_expression LE shift_expression { $$ = cs_create_binary_expression(C, LE_EXPRESSION, $1, $3); }
        ;

shift_expression
        : additive_expression
        | shift_expression LSHIFT additive_expression { $$ = cs_create_binary_expression(C, LSHIFT_EXPRESSION, $1, $3); }
        | shift_expression RSHIFT additive_expression { $$ = cs_create_binary_expression(C, RSHIFT_EXPRESSION, $1, $3); }
        ;

additive_expression
        : multiplicative_expression
        | additive_expression ADD multiplicative_expression  { $$ = cs_create_binary_expression(C, ADD_EXPRESSION, $1, $3); }
        | additive_expression SUB multiplicative_expression  { $$ = cs_create_binary_expression(C, SUB_EXPRESSION, $1, $3); }
        ;

multiplicative_expression
        : cast_expression
        | multiplicative_expression MUL cast_expression { $$ = cs_create_binary_expression(C, MUL_EXPRESSION, $1, $3); }
        | multiplicative_expression DIV cast_expression { $$ = cs_create_binary_expression(C, DIV_EXPRESSION, $1, $3); }
        | multiplicative_expression MOD cast_expression { $$ = cs_create_binary_expression(C, MOD_EXPRESSION, $1, $3); }
        ;

/* Cast expression: (type)expr
 * All casts use safe_unary_expression for uniform, conflict-free parsing.
 * This means *, -, +, & after cast require explicit parens:
 *   (int)*x  -> (int)(*x)
 *   (int)-x  -> (int)(-x)
 *   (Foo *)&x -> (Foo *)(&x)
 */
cast_expression
        : unary_expression
        /* Built-in type cast: (int)x, (int *)x, (struct Foo *)x, (unsigned int)x */
        | LP base_type_def pointer_opt RP safe_unary_expression
        {
            ParsedType *base = $2;
            ParsedType *full_type = base;
            if ($3 > 0) {
                full_type = cs_wrap_parsed_pointer(base, $3);
            }
            $$ = cs_create_type_cast_expression(C, full_type, $5);
        }
        /* Const built-in cast: (const int)x, (const int *)x */
        | LP CONST_T base_type_def pointer_opt RP safe_unary_expression
        {
            ParsedType *base = cs_copy_parsed_type($3);
            cs_parsed_type_set_const(base, true);
            ParsedType *full_type = base;
            if ($4 > 0) {
                full_type = cs_wrap_parsed_pointer(base, $4);
            }
            $$ = cs_create_type_cast_expression(C, full_type, $6);
        }
        /* Const typedef cast: (const Foo)x, (const Foo *)x */
        | LP CONST_T IDENTIFIER pointer_opt RP safe_unary_expression
        {
            ParsedType *base = resolve_typedef($3);
            cs_parsed_type_set_const(base, true);
            ParsedType *full_type = base;
            if ($4 > 0) {
                full_type = cs_wrap_parsed_pointer(base, $4);
            }
            $$ = cs_create_type_cast_expression(C, full_type, $6);
        }
        /* Typedef pointer cast: (Foo *)x */
        | LP IDENTIFIER pointer RP safe_unary_expression
        {
            ParsedType *base = resolve_typedef($2);
            ParsedType *full_type = cs_wrap_parsed_pointer(base, $3);
            $$ = cs_create_type_cast_expression(C, full_type, $5);
        }
        /* Typedef non-pointer cast: (Foo)x */
        | LP IDENTIFIER RP safe_unary_expression
        {
            ParsedType *base = resolve_typedef($2);
            $$ = cs_create_type_cast_expression(C, base, $4);
        }
        ;

unary_expression
        : postfix_expression
        | INCREMENT unary_expression    { $$ = cs_create_inc_dec_expression(C, $2, INCREMENT_EXPRESSION, true); }
        | DECREMENT unary_expression    { $$ = cs_create_inc_dec_expression(C, $2, DECREMENT_EXPRESSION, true); }
        | ADD cast_expression           { $$ = cs_create_plus_expression(C, $2); }
        | SUB cast_expression           { $$ = cs_create_minus_expression(C, $2); }
        | EXCLAMATION cast_expression   { $$ = cs_create_logical_not_expression(C, $2); }
        | TILDE cast_expression         { $$ = cs_create_bit_not_expression(C, $2); }
        | BIT_AND cast_expression       { $$ = cs_create_address_expression(C, $2); }
        | MUL cast_expression           { $$ = cs_create_dereference_expression(C, $2); }
        /* sizeof(type) only - no sizeof expr (Cminor: sizeof only for calloc)
         * Uses type_ref which includes both built-in types and typedef names */
        | SIZEOF LP type_ref pointer_opt RP
        {
            ParsedType *base = $3;
            ParsedType *full_type = base;
            if ($4 > 0) {
                full_type = cs_wrap_parsed_pointer(base, $4);
            }
            $$ = cs_create_sizeof_expression(C, full_type, NULL, true);
        }
        /* sizeof identifier - for array element count */
        | SIZEOF IDENTIFIER
        {
            Expression *id_expr = cs_create_identifier_expression(C, $2);
            $$ = cs_create_sizeof_expression(C, NULL, id_expr, false);
        }
        /* sizeof *arr, sizeof **arr, etc. - returns 1 */
        | SIZEOF pointer IDENTIFIER
        {
            Expression *expr = cs_create_identifier_expression(C, $3);
            /* Apply $2 levels of dereference */
            for (int i = 0; i < $2; i++) {
                expr = cs_create_dereference_expression(C, expr);
            }
            $$ = cs_create_sizeof_expression(C, NULL, expr, false);
        }
        ;

/* safe_unary_expression: unary_expression without *, -, +, &
 * These are excluded because they conflict with binary operators:
 * - (Foo)*x  -> parsed as multiply: (Foo) * x
 * - (Foo)-x  -> parsed as subtract: (Foo) - x
 * - (Foo)+x  -> parsed as add: (Foo) + x
 * - (Foo)&x  -> parsed as bitwise AND: (Foo) & x
 * Use explicit parens for these: (Foo)(*x), (Foo)(-x), etc.
 * For chained casts: (Foo)((Bar)x) - explicit parens required */
safe_unary_expression
        : postfix_expression
        | INCREMENT unary_expression    { $$ = cs_create_inc_dec_expression(C, $2, INCREMENT_EXPRESSION, true); }
        | DECREMENT unary_expression    { $$ = cs_create_inc_dec_expression(C, $2, DECREMENT_EXPRESSION, true); }
        | EXCLAMATION cast_expression   { $$ = cs_create_logical_not_expression(C, $2); }
        | TILDE cast_expression         { $$ = cs_create_bit_not_expression(C, $2); }
        | SIZEOF LP type_ref pointer_opt RP
        {
            ParsedType *base = $3;
            ParsedType *full_type = base;
            if ($4 > 0) {
                full_type = cs_wrap_parsed_pointer(base, $4);
            }
            $$ = cs_create_sizeof_expression(C, full_type, NULL, true);
        }
        /* sizeof identifier - for array element count */
        | SIZEOF IDENTIFIER
        {
            Expression *id_expr = cs_create_identifier_expression(C, $2);
            $$ = cs_create_sizeof_expression(C, NULL, id_expr, false);
        }
        ;

postfix_expression
        : primary_expression
        | postfix_expression LP argument_list RP     { $$ = cs_create_function_call_expression(C, $1, $3); }
        | postfix_expression LP RP     { $$ = cs_create_function_call_expression(C, $1, NULL); }
        | postfix_expression INCREMENT { $$ = cs_create_inc_dec_expression(C, $1, INCREMENT_EXPRESSION, false);}
        | postfix_expression DECREMENT { $$ = cs_create_inc_dec_expression(C, $1, DECREMENT_EXPRESSION, false);}
        | postfix_expression LBRACKET expression RBRACKET { $$ = cs_create_array_expression(C, $1, $3); }
        | postfix_expression DOT IDENTIFIER { $$ = cs_create_member_expression(C, $1, $3, false); }
        | postfix_expression ARROW IDENTIFIER { $$ = cs_create_member_expression(C, $1, $3, true); }
        ;

primary_expression
    : LP expression RP { $$ = $2;}
    /* Parenthesized identifier: (x) - bison prefers shift for cast, this handles non-cast */
    | LP IDENTIFIER RP { $$ = cs_create_identifier_expression(C, $2); }
    | IDENTIFIER       { $$ = cs_create_identifier_expression(C, $1); }
    | TRUE_T           { $$ = cs_create_bool_expression(C, true); }
    | FALSE_T          { $$ = cs_create_bool_expression(C, false); }
    | NULL_T           { $$ = cs_create_null_expression(C); }
    | INT_LITERAL      { $$ = cs_create_int_expression(C, $1); }
    | UINT_LITERAL     { $$ = cs_create_uint_expression(C, $1); }
    | LONG_LITERAL     { $$ = cs_create_long_expression(C, $1); }
    | ULONG_LITERAL    { $$ = cs_create_ulong_expression(C, $1); }
    | DOUBLE_LITERAL   { $$ = cs_create_double_expression(C, $1); }
    | FLOAT_LITERAL    { $$ = cs_create_float_expression(C, $1); }
    | STRING_LITERAL   { $$ = cs_create_string_expression(C, $1); }
    ;
%%
int
yyerror(YYLTYPE *yylloc, Scanner *scanner, char const *str)
{
    const char *filename = NULL;
    if (yylloc && yylloc->filename)
    {
        filename = yylloc->filename;
    }
    int line = 0;
    if (yylloc && yylloc->first_line > 0)
    {
        line = yylloc->first_line;
    }
    else if (scanner)
    {
        line = get_current_line(scanner);
    }

    if (filename && line > 0)
    {
        fprintf(stderr, "%s:%d: ", filename, line);
    }
    else if (line > 0)
    {
        fprintf(stderr, "line %d: ", line);
    }

    const char *text = cs_scanner_text(scanner);
    if (text && text[0])
    {
        fprintf(stderr, "parser error near '%s': %s\n", text, str);
    }
    else
    {
        fprintf(stderr, "parser error: %s\n", str);
    }
    return 0;
}
