#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codegenvisitor.h"
#include "codegenvisitor_util.h"
#include "codegenvisitor_stmt_util.h"
#include "codebuilder_ptr.h"
#include "codebuilder_control.h"
#include "codebuilder_internal.h"
#include "codebuilder_label.h"
#include "codebuilder_part1.h"
#include "codegen_constants.h"
#include "codegen_jvm_types.h"
#include "cminor_type.h"

void ensure_if_capacity(CodegenVisitor *v)
{
    if (v->ctx.if_capacity > v->ctx.if_depth)
    {
        return;
    }

    int new_cap = v->ctx.if_capacity ? v->ctx.if_capacity * 2 : 4;
    CodegenIfContext *new_stack = (CodegenIfContext *)calloc(new_cap, sizeof(CodegenIfContext));
    for (int i = 0; i < v->ctx.if_depth; i++)
    {
        new_stack[i] = v->ctx.if_stack[i];
    }
    v->ctx.if_stack = new_stack;
    v->ctx.if_capacity = new_cap;
}

void ensure_for_capacity(CodegenVisitor *v)
{
    if (v->ctx.for_capacity > v->ctx.for_depth)
    {
        return;
    }

    int new_cap = v->ctx.for_capacity ? v->ctx.for_capacity * 2 : 4;
    CodegenForContext *new_stack = (CodegenForContext *)calloc(new_cap, sizeof(CodegenForContext));
    for (int i = 0; i < v->ctx.for_depth; i++)
    {
        new_stack[i] = v->ctx.for_stack[i];
    }
    v->ctx.for_stack = new_stack;
    v->ctx.for_capacity = new_cap;
}

void ensure_switch_capacity(CodegenVisitor *v)
{
    if (v->ctx.switch_capacity > v->ctx.switch_depth)
    {
        return;
    }

    int new_cap = v->ctx.switch_capacity ? v->ctx.switch_capacity * 2 : 4;
    CodegenSwitchContext *new_stack = (CodegenSwitchContext *)calloc(new_cap, sizeof(CodegenSwitchContext));
    for (int i = 0; i < v->ctx.switch_depth; i++)
    {
        new_stack[i] = v->ctx.switch_stack[i];
    }
    v->ctx.switch_stack = new_stack;
    v->ctx.switch_capacity = new_cap;
}

CodegenIfContext *push_if_context(CodegenVisitor *v, Statement *stmt)
{
    ensure_if_capacity(v);
    CodegenIfContext *ctx = &v->ctx.if_stack[v->ctx.if_depth++];
    ctx->if_stmt = stmt;
    ctx->then_stmt = stmt ? stmt->u.if_s.then_statement : NULL;
    ctx->else_stmt = stmt ? stmt->u.if_s.else_statement : NULL;
    ctx->then_block = codebuilder_create_label(v->builder);
    ctx->else_block = ctx->else_stmt ? codebuilder_create_label(v->builder) : NULL;
    ctx->end_block = codebuilder_create_label(v->builder);
    ctx->has_cond_branch = false;
    ctx->then_alive = false;
    ctx->else_alive = false;
    return ctx;
}

CodegenIfContext pop_if_context(CodegenVisitor *v, Statement *stmt)
{
    if (v->ctx.if_depth == 0)
    {
        fprintf(stderr, "if context underflow\n");
        exit(1);
    }

    CodegenIfContext ctx = v->ctx.if_stack[--v->ctx.if_depth];
    if (ctx.if_stmt != stmt)
    {
        fprintf(stderr, "mismatched if context pop\n");
        exit(1);
    }
    return ctx;
}

CodegenForContext *push_loop_context(CodegenVisitor *v, Statement *stmt,
                                     Statement *body, Expression *condition,
                                     Expression *post)
{
    ensure_for_capacity(v);

    /* Push CodeBuilder's loop context and set up labels */
    CB_ControlEntry *entry = codebuilder_push_loop_raw(v->builder);
    entry->u.loop_ctx.cond_label = codebuilder_create_label(v->builder);
    entry->u.loop_ctx.body_label = codebuilder_create_label(v->builder);
    entry->u.loop_ctx.post_label = post ? codebuilder_create_label(v->builder) : NULL;
    entry->u.loop_ctx.end_label = codebuilder_create_label(v->builder);
    entry->u.loop_ctx.continue_label = post ? entry->u.loop_ctx.post_label : entry->u.loop_ctx.cond_label;
    entry->u.loop_ctx.has_post = (post != NULL);

    /* Push Visitor's context (AST info only) */
    CodegenForContext *ctx = &v->ctx.for_stack[v->ctx.for_depth++];
    ctx->for_stmt = stmt;
    ctx->body_stmt = body;
    ctx->condition_expr = condition;
    ctx->post_expr = post;
    ctx->is_do_while = false;
    ctx->has_cond_branch = false;
    ctx->body_alive = false;

    return ctx;
}

CodegenForContext *push_for_context(CodegenVisitor *v, Statement *stmt)
{
    return push_loop_context(v, stmt, stmt ? stmt->u.for_s.body : NULL,
                             stmt ? stmt->u.for_s.condition : NULL,
                             stmt ? stmt->u.for_s.post : NULL);
}

CodegenForContext *push_while_context(CodegenVisitor *v, Statement *stmt)
{
    return push_loop_context(v, stmt, stmt ? stmt->u.while_s.body : NULL,
                             stmt ? stmt->u.while_s.condition : NULL, NULL);
}

CodegenSwitchContext *push_switch_context(CodegenVisitor *v, Statement *stmt)
{
    ensure_switch_capacity(v);

    /* Push CodeBuilder's switch context and set up labels */
    CB_ControlEntry *entry = codebuilder_push_switch_raw(v->builder);
    entry->u.switch_ctx.dispatch_label = codebuilder_create_label(v->builder);
    entry->u.switch_ctx.end_label = codebuilder_create_label(v->builder);

    /* Push Visitor's context (AST info only) */
    CodegenSwitchContext *ctx = &v->ctx.switch_stack[v->ctx.switch_depth++];
    ctx->switch_stmt = stmt;
    ctx->body_stmt = stmt ? stmt->u.switch_s.body : NULL;
    ctx->expression = stmt ? stmt->u.switch_s.expression : NULL;
    ctx->expr_tag = CF_VAL_INT; /* Default, will be set in handle_switch_entry */
    ctx->has_expr_local = false;
    ctx->has_dispatch_goto = false;
    ctx->any_case_alive = false;

    return ctx;
}

CodegenSwitchContext pop_switch_context(CodegenVisitor *v, Statement *stmt)
{
    if (v->ctx.switch_depth == 0)
    {
        fprintf(stderr, "switch context underflow\n");
        exit(1);
    }

    CodegenSwitchContext ctx = v->ctx.switch_stack[--v->ctx.switch_depth];
    if (ctx.switch_stmt != stmt)
    {
        fprintf(stderr, "mismatched switch context pop\n");
        exit(1);
    }

    /* Note: CodeBuilder's switch context is NOT popped here.
     * Caller must call codebuilder_pop_switch_raw() after using switch data. */

    return ctx;
}

CodegenForContext pop_for_context(CodegenVisitor *v, Statement *stmt)
{
    if (v->ctx.for_depth == 0)
    {
        fprintf(stderr, "for context underflow\n");
        exit(1);
    }

    CodegenForContext ctx = v->ctx.for_stack[--v->ctx.for_depth];
    if (ctx.for_stmt != stmt)
    {
        fprintf(stderr, "mismatched for context pop\n");
        exit(1);
    }

    /* Pop CodeBuilder's loop context */
    codebuilder_pop_loop_raw(v->builder);

    return ctx;
}

void handle_if_boundary(CodegenVisitor *v, Statement *stmt)
{
    for (int32_t i = (int32_t)v->ctx.if_depth - 1; i >= 0; --i)
    {
        CodegenIfContext *ctx = &v->ctx.if_stack[i];
        if (ctx->then_stmt == stmt && !ctx->has_cond_branch)
        {
            /* Skip condition branch if dead - no condition value on stack */
            if (!v->builder->alive)
            {
                /* Dead code path - just place labels without condition branch */
                codebuilder_place_label(v->builder, ctx->then_block);
                ctx->has_cond_branch = true;
                break;
            }
            /* If condition is false (0/null), jump to else/end block; else fall through to then */
            CB_Label *false_block = ctx->else_block ? ctx->else_block : ctx->end_block;
            /* Use ifnull on .base for pointer conditions, ifeq for bool/int */
            TypeSpecifier *cond_type = ctx->if_stmt->u.if_s.condition->type;
            if (cs_type_is_pointer(cond_type))
            {
                if (cs_type_is_void_pointer(cond_type))
                {
                    fprintf(stderr, "void* condition not supported\n");
                    exit(1);
                }
                /* Pointer wrapper: check if .base field is null */
                cg_emit_ptr_get_base(v, cond_type);
                codebuilder_jump_if_null(v->builder, false_block);
            }
            else if (cs_type_is_array(cond_type))
            {
                codebuilder_jump_if_null(v->builder, false_block);
            }
            else
            {
                codebuilder_jump_if_not(v->builder, false_block);
            }
            ctx->has_cond_branch = true;
            codebuilder_place_label(v->builder, ctx->then_block);
            break;
        }
        if (ctx->else_stmt && ctx->else_stmt == stmt && ctx->has_cond_branch)
        {
            /* Save then block's alive state before jumping to end */
            ctx->then_alive = v->builder->alive;
            codebuilder_jump(v->builder, ctx->end_block);
            codebuilder_place_label(v->builder, ctx->else_block);
            break;
        }
    }
}

void handle_for_body_entry(CodegenVisitor *v, Statement *stmt)
{
    for (int32_t i = (int32_t)v->ctx.for_depth - 1; i >= 0; --i)
    {
        CodegenForContext *ctx = &v->ctx.for_stack[i];
        if (ctx->body_stmt != stmt)
        {
            continue;
        }

        /* Get labels from CodeBuilder's loop context */
        CB_ControlEntry *entry = codebuilder_current_loop(v->builder);
        if (!entry)
        {
            fprintf(stderr, "no loop context in CodeBuilder\n");
            exit(1);
        }
        if (ctx->is_do_while)
        {
            if (!entry->u.loop_ctx.body_label->is_placed)
            {
                codebuilder_place_label(v->builder, entry->u.loop_ctx.body_label);
            }
            break;
        }

        /* Dead code path - just place labels without condition branch */
        if (!v->builder->alive)
        {
            if (!entry->u.loop_ctx.cond_label->is_placed)
            {
                codebuilder_place_label(v->builder, entry->u.loop_ctx.cond_label);
            }
            codebuilder_place_label(v->builder, entry->u.loop_ctx.body_label);
            ctx->has_cond_branch = true;
            break;
        }

        if (ctx->condition_expr && !entry->u.loop_ctx.cond_label->is_placed)
        {
            fprintf(stderr, "loop condition block not positioned\n");
            exit(1);
        }

        if (!ctx->condition_expr && !entry->u.loop_ctx.cond_label->is_placed)
        {
            codebuilder_place_label(v->builder, entry->u.loop_ctx.cond_label);
        }

        if (!ctx->has_cond_branch)
        {
            if (ctx->condition_expr)
            {
                /* If condition is false (0/null), jump to end_label; else fall through to body */
                /* Use ifnull on .base for pointer conditions, ifeq for bool/int */
                TypeSpecifier *cond_type = ctx->condition_expr->type;
                if (cs_type_is_pointer(cond_type))
                {
                    if (cs_type_is_void_pointer(cond_type))
                    {
                        fprintf(stderr, "void* condition not supported\n");
                        exit(1);
                    }
                    /* Pointer wrapper: check if .base field is null */
                    cg_emit_ptr_get_base(v, cond_type);
                    codebuilder_jump_if_null(v->builder, entry->u.loop_ctx.end_label);
                }
                else if (cs_type_is_array(cond_type))
                {
                    codebuilder_jump_if_null(v->builder, entry->u.loop_ctx.end_label);
                }
                else
                {
                    codebuilder_jump_if_not(v->builder, entry->u.loop_ctx.end_label);
                }
            }
            else
            {
                /* Infinite loop - just fall through to body */
            }
            ctx->has_cond_branch = true;
        }

        codebuilder_place_label(v->builder, entry->u.loop_ctx.body_label);
        break;
    }
}

void handle_switch_entry(CodegenVisitor *v, Statement *stmt)
{
    for (int32_t i = (int32_t)v->ctx.switch_depth - 1; i >= 0; --i)
    {
        CodegenSwitchContext *ctx = &v->ctx.switch_stack[i];
        if (ctx->body_stmt != stmt || ctx->has_dispatch_goto)
        {
            continue;
        }

        /* Dead code path - skip switch entry setup */
        if (!v->builder->alive)
        {
            ctx->has_dispatch_goto = true;
            break;
        }

        if (!ctx->expression)
        {
            fprintf(stderr, "switch expression missing\n");
            exit(1);
        }

        if (v->builder->frame->stack_count == 0)
        {
            fprintf(stderr, "switch expression value missing on stack\n");
            exit(1);
        }

        /* Get labels from CodeBuilder's switch context */
        CB_ControlEntry *entry = codebuilder_current_switch(v->builder);
        if (!entry)
        {
            fprintf(stderr, "no switch context in CodeBuilder\n");
            exit(1);
        }
        ctx->expr_tag = cg_to_value_tag(ctx->expression->type);

        int expr_local = allocate_temp_local_for_tag(v, ctx->expr_tag);
        entry->u.switch_ctx.expr_local = expr_local;
        ctx->has_expr_local = true;

        switch (ctx->expr_tag)
        {
        case CF_VAL_INT:
            codebuilder_build_istore(v->builder, expr_local);
            break;
        case CF_VAL_LONG:
            codebuilder_build_lstore(v->builder, expr_local);
            break;
        case CF_VAL_FLOAT:
            codebuilder_build_fstore(v->builder, expr_local);
            break;
        case CF_VAL_DOUBLE:
            codebuilder_build_dstore(v->builder, expr_local);
            break;
        case CF_VAL_OBJECT:
            codebuilder_build_astore(v->builder, expr_local);
            break;
        default:
            fprintf(stderr, "handle_switch_entry: invalid expr_tag %d in %s\n",
                    ctx->expr_tag, v->builder->method_name ? v->builder->method_name : "<unknown>");
            exit(1);
        }

        /* Save frame state at switch entry for case labels.
         * After the dispatch jump, alive becomes false. When case labels are
         * placed, they need this saved frame to restore alive and ensure
         * code inside cases (like goto) is emitted correctly. */
        entry->u.switch_ctx.entry_frame = cb_create_frame();
        cb_copy_frame(entry->u.switch_ctx.entry_frame, v->builder->frame);

        codebuilder_jump(v->builder, entry->u.switch_ctx.dispatch_label);
        ctx->has_dispatch_goto = true;
        break;
    }
}

int eval_case_value(Expression *expr)
{
    if (!expr)
    {
        fprintf(stderr, "case expression missing\n");
        exit(1);
    }

    if (expr->kind == INT_EXPRESSION || expr->kind == UINT_EXPRESSION)
    {
        return expr->u.int_value;
    }

    /* Enum constant: use value */
    if (expr->kind == IDENTIFIER_EXPRESSION &&
        expr->u.identifier.is_enum_member &&
        expr->u.identifier.u.enum_member)
    {
        return expr->u.identifier.u.enum_member->value;
    }

    /* Non-enum identifier that has been constant-folded: check type for char */
    if (expr->kind == IDENTIFIER_EXPRESSION)
    {
        const char *name = expr->u.identifier.name;
        fprintf(stderr, "unsupported case expression: identifier '%s' (is_enum_member=%d)\n",
                name ? name : "(null)", expr->u.identifier.is_enum_member);
        exit(1);
    }

    fprintf(stderr, "unsupported case expression kind %d\n", expr->kind);
    exit(1);
}

bool is_vla_type(TypeSpecifier *type)
{
    if (!type || !cs_type_is_array(type))
    {
        return false;
    }
    Expression *size_expr = cs_type_array_size(type);
    if (!size_expr)
    {
        return false;
    }
    /* VLA if size is not a constant expression */
    return size_expr->kind != INT_EXPRESSION && size_expr->kind != BOOL_EXPRESSION;
}
