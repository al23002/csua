#include <stdio.h>
#include <stdlib.h>

#include "codegenvisitor.h"
#include "codegenvisitor_util.h"
#include "codegenvisitor_expr_util.h"
#include "codebuilder_control.h"
#include "codebuilder_internal.h"
#include "codebuilder_label.h"
#include "codebuilder_part1.h"
#include "codebuilder_part2.h"
#include "codebuilder_part3.h"
#include "cminor_type.h"
#include "classfile_opcode.h"

int count_initializer_list(ExpressionList *list)
{
    int count = 0;
    for (ExpressionList *p = list; p; p = p->next)
    {
        ++count;
    }
    return count;
}

int count_nested_initializer_values(ExpressionList *list)
{
    int count = 0;
    for (ExpressionList *p = list; p; p = p->next)
    {
        if (p->expression && p->expression->kind == INITIALIZER_LIST_EXPRESSION)
        {
            count += count_nested_initializer_values(p->expression->u.initializer_list);
        }
        else
        {
            ++count;
        }
    }
    return count;
}

bool is_primitive_array(TypeSpecifier *type)
{
    if (!type || !cs_type_is_array(type))
    {
        return false;
    }
    TypeSpecifier *elem = cs_type_child(type);
    return elem && cs_type_is_primitive(elem);
}

void mark_for_condition_start(CodegenVisitor *v, Expression *expr)
{
    for (int32_t i = (int32_t)v->ctx.for_depth - 1; i >= 0; --i)
    {
        CodegenForContext *ctx = &v->ctx.for_stack[i];

        /* Get labels from CodeBuilder's loop context */
        CB_ControlEntry *entry = codebuilder_current_loop(v->builder);
        if (!entry)
        {
            continue;
        }

        if (ctx->condition_expr == expr && entry->u.loop_ctx.cond_label && !entry->u.loop_ctx.cond_label->is_placed)
        {
            codebuilder_place_label(v->builder, entry->u.loop_ctx.cond_label);
            break;
        }

        if (ctx->post_expr == expr && entry->u.loop_ctx.post_label && !entry->u.loop_ctx.post_label->is_placed)
        {
            codebuilder_place_label(v->builder, entry->u.loop_ctx.post_label);
            break;
        }
    }
}

void handle_for_expression_leave(CodegenVisitor *v, Expression *expr)
{
    for (int32_t i = (int32_t)v->ctx.for_depth - 1; i >= 0; --i)
    {
        CodegenForContext *ctx = &v->ctx.for_stack[i];
        if (ctx->post_expr != expr)
        {
            continue;
        }

        if (v->builder->frame->stack_count > 0)
        {
            /* Use pop_value to correctly handle category 2 types (long, double) */
            codebuilder_build_pop_value(v->builder);
        }

        /* Get labels from CodeBuilder's loop context */
        CB_ControlEntry *entry = codebuilder_current_loop(v->builder);
        if (!entry)
        {
            fprintf(stderr, "for loop condition target missing\n");
            exit(1);
        }
        if (!entry->u.loop_ctx.cond_label)
        {
            fprintf(stderr, "for loop condition label missing\n");
            exit(1);
        }

        codebuilder_jump(v->builder, entry->u.loop_ctx.cond_label);
        break;
    }
}

void emit_if_comparison(CodegenVisitor *v, IfCond cond)
{
    CB_Label *true_block = codebuilder_create_label(v->builder);
    CB_Label *false_block = codebuilder_create_label(v->builder);
    CB_Label *end_block = codebuilder_create_label(v->builder);

    /* If condition is true, jump to true_block; else fall through to false_block */
    codebuilder_jump_if_op(v->builder, cond, true_block);

    /* Fall-through is the false path */
    codebuilder_place_label(v->builder, false_block);
    codebuilder_build_iconst(v->builder, 0);
    codebuilder_jump(v->builder, end_block);

    /* True path (jumped to from cond_br) */
    codebuilder_place_label(v->builder, true_block);
    codebuilder_build_iconst(v->builder, 1);
    /* Fall through to end_block */

    codebuilder_place_label(v->builder, end_block);
}

void emit_icmp_comparison(CodegenVisitor *v, IntCmpCond cond)
{
    CB_Label *true_block = codebuilder_create_label(v->builder);
    CB_Label *false_block = codebuilder_create_label(v->builder);
    CB_Label *end_block = codebuilder_create_label(v->builder);

    /* If condition is true, jump to true_block; else fall through to false_block */
    codebuilder_jump_if_icmp(v->builder, cond, true_block);

    /* Fall-through is the false path */
    codebuilder_place_label(v->builder, false_block);
    codebuilder_build_iconst(v->builder, 0);
    codebuilder_jump(v->builder, end_block);

    /* True path (jumped to from cond_br) */
    codebuilder_place_label(v->builder, true_block);
    codebuilder_build_iconst(v->builder, 1);
    /* Fall through to end_block */

    codebuilder_place_label(v->builder, end_block);
}

void emit_acmp_comparison(CodegenVisitor *v, ACmpCond cond)
{
    CB_Label *true_block = codebuilder_create_label(v->builder);
    CB_Label *false_block = codebuilder_create_label(v->builder);
    CB_Label *end_block = codebuilder_create_label(v->builder);

    /* If condition is true, jump to true_block; else fall through to false_block */
    codebuilder_jump_if_acmp(v->builder, cond, true_block);

    /* Fall-through is the false path */
    codebuilder_place_label(v->builder, false_block);
    codebuilder_build_iconst(v->builder, 0);
    codebuilder_jump(v->builder, end_block);

    /* True path (jumped to from cond_br) */
    codebuilder_place_label(v->builder, true_block);
    codebuilder_build_iconst(v->builder, 1);
    /* Fall through to end_block */

    codebuilder_place_label(v->builder, end_block);
}

/* Check if reference is null (check_null=true) or nonnull (check_null=false).
 * Stack: [ref] -> [0 or 1] */
void emit_if_ref_null_check(CodegenVisitor *v, bool check_null)
{
    CB_Label *null_block = codebuilder_create_label(v->builder);
    CB_Label *nonnull_block = codebuilder_create_label(v->builder);
    CB_Label *end_block = codebuilder_create_label(v->builder);

    /* ifnull jumps to null_block if reference is null */
    codebuilder_jump_if_null(v->builder, null_block);

    /* Fall-through: reference is nonnull */
    codebuilder_place_label(v->builder, nonnull_block);
    codebuilder_build_iconst(v->builder, check_null ? 0 : 1);
    codebuilder_jump(v->builder, end_block);

    /* Null path */
    codebuilder_place_label(v->builder, null_block);
    codebuilder_build_iconst(v->builder, check_null ? 1 : 0);

    codebuilder_place_label(v->builder, end_block);
}
