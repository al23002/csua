#pragma once

#include "cminor_base.h"

typedef struct CodegenVisitor_tag CodegenVisitor;
typedef struct CodegenIfContext_tag CodegenIfContext;
typedef struct CodegenForContext_tag CodegenForContext;
typedef struct CodegenSwitchContext_tag CodegenSwitchContext;

/* Context management functions */
void ensure_if_capacity(CodegenVisitor *v);
void ensure_for_capacity(CodegenVisitor *v);
void ensure_switch_capacity(CodegenVisitor *v);

CodegenIfContext *push_if_context(CodegenVisitor *v, Statement *stmt);
CodegenIfContext pop_if_context(CodegenVisitor *v, Statement *stmt);

CodegenForContext *push_loop_context(CodegenVisitor *v, Statement *stmt,
                                     Statement *body, Expression *condition,
                                     Expression *post);
CodegenForContext *push_for_context(CodegenVisitor *v, Statement *stmt);
CodegenForContext *push_while_context(CodegenVisitor *v, Statement *stmt);
CodegenForContext pop_for_context(CodegenVisitor *v, Statement *stmt);

CodegenSwitchContext *push_switch_context(CodegenVisitor *v, Statement *stmt);
CodegenSwitchContext pop_switch_context(CodegenVisitor *v, Statement *stmt);

/* Statement boundary handlers */
void handle_if_boundary(CodegenVisitor *v, Statement *stmt);
void handle_for_body_entry(CodegenVisitor *v, Statement *stmt);
void handle_switch_entry(CodegenVisitor *v, Statement *stmt);

/* Helper functions */
int32_t eval_case_value(Expression *expr);
bool is_vla_type(TypeSpecifier *type);
