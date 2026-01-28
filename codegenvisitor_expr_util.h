#pragma once

#include "classfile_opcode.h"
#include "cminor_base.h"

typedef struct CodegenVisitor_tag CodegenVisitor;

int count_initializer_list(ExpressionList *list);
int count_nested_initializer_values(ExpressionList *list);
bool is_primitive_array(TypeSpecifier *type);
void mark_for_condition_start(CodegenVisitor *v, Expression *expr);
void handle_for_expression_leave(CodegenVisitor *v, Expression *expr);
void emit_if_comparison(CodegenVisitor *v, IfCond cond);
void emit_icmp_comparison(CodegenVisitor *v, IntCmpCond cond);
void emit_acmp_comparison(CodegenVisitor *v, ACmpCond cond);
void emit_if_ref_null_check(CodegenVisitor *v, bool check_null);
