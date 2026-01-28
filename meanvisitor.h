#pragma once

#include "ast.h"
#include "compiler.h"
#include "visitor.h"

typedef struct MeanVisitor_tag MeanVisitor;

typedef struct MeanCheckLog_tag
{
    char *log_str;
    struct MeanCheckLog_tag *next;
} MeanCheckLogger;

typedef struct Scope_tag
{
    DeclarationList *decl_list;
    struct Scope_tag *next;
} Scope;

typedef enum
{
    VISIT_NORMAL,
    VISIT_NOMAL_ASSIGN,
} VisitIdentState;

typedef enum
{
    VISIT_F_NO,
    VISIT_F_CALL,
} VisitFunCallState;

/* Stack for tracking switch expression types (for nested switches) */
typedef struct SwitchTypeStack_tag
{
    TypeSpecifier *expr_type;
    struct SwitchTypeStack_tag *next;
} SwitchTypeStack;

typedef struct MeanVisitor_tag
{
    Visitor visitor;
    CS_Compiler *compiler;
    int i;
    int j;
    int log_count;
    MeanCheckLogger *check_log_tail;
    MeanCheckLogger *check_log;
    Scope *current_scope;
    SwitchTypeStack *switch_type_stack;    /* Current switch expression type */
    FunctionDeclaration *current_function; /* Current function for return type propagation */
} MeanVisitor;

MeanVisitor *create_mean_visitor(CS_Compiler *compiler);
void show_mean_error(MeanVisitor *visitor);
char *get_type_name(CS_BasicType type);
void mean_visitor_enter_function(MeanVisitor *visitor, FunctionDeclaration *func);
void mean_visitor_leave_function(MeanVisitor *visitor);

/* Switch-based AST traversal (replaces function pointer dispatch) */
void mean_traverse_expr(Expression *expr, MeanVisitor *visitor);
void mean_traverse_stmt(Statement *stmt, MeanVisitor *visitor);
