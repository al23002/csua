#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codegenvisitor.h"
#include "codegenvisitor_stmt_control.h"
#include "codegenvisitor_util.h"
#include "codegenvisitor_stmt_util.h"
#include "codebuilder_ptr.h"
#include "codebuilder_control.h"
#include "codebuilder_core.h"
#include "codebuilder_label.h"
#include "codegen_symbols.h"
#include "codegen_constants.h"
#include "cminor_type.h"

void enter_ifstmt(Statement *stmt, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    handle_if_boundary(cg, stmt);
    handle_for_body_entry(cg, stmt);
    cg_begin_scope(cg, false);
    CodegenIfContext *ctx = push_if_context(cg, stmt);

    /* If entering if statement in dead code, mark condition branch as handled
     * (no actual branch needed since code is unreachable) */
    if (!cg->builder->alive)
    {
        ctx->has_cond_branch = true;
    }
}

void enter_whilestmt(Statement *stmt, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    handle_if_boundary(cg, stmt);
    handle_for_body_entry(cg, stmt);
    cg_begin_scope(cg, true);
    CodegenForContext *ctx = push_while_context(cg, stmt);

    /* If entering while in dead code, mark condition branch as handled */
    if (!cg->builder->alive)
    {
        ctx->has_cond_branch = true;
    }

    /* Mark and place cond_label as loop header for StackMap */
    CB_ControlEntry *entry = codebuilder_current_loop(cg->builder);
    codebuilder_mark_loop_header(cg->builder, entry->u.loop_ctx.cond_label);
    codebuilder_place_label(cg->builder, entry->u.loop_ctx.cond_label);
}

void enter_dowhilestmt(Statement *stmt, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    handle_if_boundary(cg, stmt);
    cg_begin_scope(cg, true);
    CodegenForContext *ctx =
        push_loop_context(cg, stmt, stmt ? stmt->u.do_s.body : NULL,
                          stmt ? stmt->u.do_s.condition : NULL, NULL);
    ctx->is_do_while = true;

    /* If entering do-while in dead code, mark condition branch as handled */
    if (!cg->builder->alive)
    {
        ctx->has_cond_branch = true;
    }

    /* Mark body_label as loop header and place it */
    CB_ControlEntry *entry = codebuilder_current_loop(cg->builder);
    entry->u.loop_ctx.is_do_while = true;
    codebuilder_mark_loop_header(cg->builder, entry->u.loop_ctx.body_label);
    codebuilder_place_label(cg->builder, entry->u.loop_ctx.body_label);
}

void enter_forstmt(Statement *stmt, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    handle_if_boundary(cg, stmt);
    handle_for_body_entry(cg, stmt);
    cg_begin_scope(cg, true);
    CodegenForContext *ctx = push_for_context(cg, stmt);

    /* If entering for in dead code, mark condition branch as handled */
    if (!cg->builder->alive)
    {
        ctx->has_cond_branch = true;
    }

    /* Mark cond_label as loop header for StackMap */
    CB_ControlEntry *entry = codebuilder_current_loop(cg->builder);
    codebuilder_mark_loop_header(cg->builder, entry->u.loop_ctx.cond_label);
}

void leave_ifstmt(Statement *stmt, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    CodegenIfContext ctx = pop_if_context(cg, stmt);

    if (!ctx.has_cond_branch)
    {
        fprintf(stderr, "if condition branch missing\n");
        exit(1);
    }

    /* Place else_block if it exists and wasn't placed (dead code path) */
    if (ctx.else_block && !ctx.else_block->is_placed)
    {
        codebuilder_place_label(cg->builder, ctx.else_block);
    }

    /* Compute alive states for javac-style branch merge */
    bool then_alive;
    bool else_alive;
    if (ctx.else_stmt)
    {
        /* Had else block: then_alive saved at boundary, else_alive is current */
        then_alive = ctx.then_alive;
        else_alive = cg->builder->alive;
    }
    else
    {
        /* No else block: then_alive is current, implicit else path is reachable
         * only if condition branch was emitted (condition could be false) */
        then_alive = cg->builder->alive;
        else_alive = ctx.end_block->frame_saved;
    }

    codebuilder_place_label(cg->builder, ctx.end_block);

    /* Apply merged alive state (javac-style: alive = then_alive || else_alive) */
    if (then_alive || else_alive)
    {
        codebuilder_mark_alive(cg->builder);
    }
    else
    {
        codebuilder_mark_dead(cg->builder);
    }

    cg_end_scope(cg, "if statement");
}

void leave_forstmt(Statement *stmt, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;

    /* Get CodeBuilder's loop context before popping */
    CB_ControlEntry *entry = codebuilder_current_loop(cg->builder);
    CB_Label *cond_label = entry->u.loop_ctx.cond_label;
    CB_Label *body_label = entry->u.loop_ctx.body_label;
    CB_Label *post_label = entry->u.loop_ctx.post_label;
    CB_Label *end_label = entry->u.loop_ctx.end_label;
    bool has_post = entry->u.loop_ctx.has_post;

    CodegenForContext ctx = pop_for_context(cg, stmt);

    /* Place any unplaced labels (dead code path) */
    if (!cond_label->is_placed)
    {
        codebuilder_place_label(cg->builder, cond_label);
    }
    if (!body_label->is_placed)
    {
        codebuilder_place_label(cg->builder, body_label);
    }
    if (post_label && !post_label->is_placed)
    {
        codebuilder_place_label(cg->builder, post_label);
    }

    if (!has_post)
    {
        codebuilder_jump(cg->builder, cond_label);
    }

    codebuilder_place_label(cg->builder, end_label);

    cg_end_scope(cg, "for statement");
}

void leave_whilestmt(Statement *stmt, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;

    /* Get CodeBuilder's loop context before popping */
    CB_ControlEntry *entry = codebuilder_current_loop(cg->builder);
    CB_Label *cond_label = entry->u.loop_ctx.cond_label;
    CB_Label *body_label = entry->u.loop_ctx.body_label;
    CB_Label *end_label = entry->u.loop_ctx.end_label;

    CodegenForContext ctx = pop_for_context(cg, stmt);

    if (ctx.post_expr != NULL)
    {
        fprintf(stderr, "while loop should not have a post expression\n");
        exit(1);
    }

    /* Place any unplaced labels (dead code path) */
    if (!cond_label->is_placed)
    {
        codebuilder_place_label(cg->builder, cond_label);
    }
    if (!body_label->is_placed)
    {
        codebuilder_place_label(cg->builder, body_label);
    }

    /* If condition branch wasn't emitted (e.g., empty body), emit it now */
    if (!ctx.has_cond_branch && ctx.condition_expr && cg->builder->alive)
    {
        TypeSpecifier *cond_type = ctx.condition_expr->type;
        if (cs_type_is_pointer(cond_type))
        {
            if (cs_type_is_void_pointer(cond_type))
            {
                fprintf(stderr, "void* condition not supported\n");
                exit(1);
            }
            cg_emit_ptr_get_base(cg, cond_type);
            codebuilder_jump_if_null(cg->builder, end_label);
        }
        else if (cs_type_is_array(cond_type))
        {
            codebuilder_jump_if_null(cg->builder, end_label);
        }
        else
        {
            codebuilder_jump_if_not(cg->builder, end_label);
        }
    }

    codebuilder_jump(cg->builder, cond_label);
    codebuilder_place_label(cg->builder, end_label);

    cg_end_scope(cg, "while statement");
}

void leave_dowhilestmt(Statement *stmt, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;

    /* Get CodeBuilder's loop context before popping */
    CB_ControlEntry *entry = codebuilder_current_loop(cg->builder);
    CB_Label *body_label = entry->u.loop_ctx.body_label;
    CB_Label *cond_label = entry->u.loop_ctx.cond_label;
    CB_Label *end_label = entry->u.loop_ctx.end_label;

    pop_for_context(cg, stmt);

    /*
     * Save alive state BEFORE placing labels.
     * Placing cond_label may restore frame and mark alive (if frame_saved),
     * but the condition expression was only evaluated if alive BEFORE labels.
     */
    bool was_alive_before_labels = cg->builder->alive;

    /* Place any unplaced labels (dead code path) */
    if (!body_label->is_placed)
    {
        codebuilder_place_label(cg->builder, body_label);
    }
    if (!cond_label->is_placed)
    {
        codebuilder_place_label(cg->builder, cond_label);
    }

    /* If body ended unreachable (e.g., do { goto X; } while(0)), skip condition jump.
     * Use saved alive state because cond_label placement may have made us alive. */
    if (!was_alive_before_labels)
    {
        /* Code is unreachable - just mark end_label for any continue statements */
        codebuilder_place_label(cg->builder, end_label);
        cg_end_scope(cg, "do-while statement");
        return;
    }

    /* If condition is true (non-zero/non-null), jump back to body; else fall through to end */
    /* Use ifnonnull on .base for pointer conditions, ifne for bool/int */
    TypeSpecifier *cond_type = stmt->u.do_s.condition->type;
    if (cs_type_is_pointer(cond_type))
    {
        if (cs_type_is_void_pointer(cond_type))
        {
            fprintf(stderr, "void* condition not supported\n");
            exit(1);
        }
        /* Pointer wrapper: check if .base field is non-null */
        cg_emit_ptr_get_base(cg, cond_type);
        codebuilder_jump_if_not_null(cg->builder, body_label);
    }
    else if (cs_type_is_array(cond_type))
    {
        codebuilder_jump_if_not_null(cg->builder, body_label);
    }
    else
    {
        codebuilder_jump_if(cg->builder, body_label);
    }
    codebuilder_place_label(cg->builder, end_label);

    cg_end_scope(cg, "do-while statement");
}
