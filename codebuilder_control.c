/*
 * CodeBuilder Control - Control Flow Structures
 *
 * Handles:
 * - If/else statements
 * - While/do-while/for loops
 * - Switch statements
 * - Break/continue statements
 * - Control stack management
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codebuilder_control.h"
#include "codebuilder_core.h"
#include "codebuilder_internal.h"
#include "codebuilder_label.h"
#include "codebuilder_part1.h"
#include "codebuilder_part2.h"
#include "codebuilder_part3.h"
#include "classfile_opcode.h"

/* ============================================================
 * Control Stack Operations
 * ============================================================ */

void cb_ensure_control_capacity(CodeBuilder *builder)
{
    if (builder->control_depth < builder->control_capacity)
    {
        return;
    }

    int new_capacity = builder->control_capacity == 0
                           ? 8
                           : builder->control_capacity * 2;
    CB_ControlEntry *new_stack = (CB_ControlEntry *)calloc(
        new_capacity, sizeof(CB_ControlEntry));
    for (int i = 0; i < builder->control_depth; i++)
    {
        new_stack[i] = builder->control_stack[i];
    }
    builder->control_stack = new_stack;
    builder->control_capacity = new_capacity;
}

CB_ControlEntry *cb_push_control(CodeBuilder *builder, CB_ControlKind kind)
{
    cb_ensure_control_capacity(builder);
    CB_ControlEntry *entry = &builder->control_stack[builder->control_depth++];
    entry->kind = kind;
    return entry;
}

CB_ControlEntry *cb_top_control(CodeBuilder *builder)
{
    if (builder->control_depth == 0)
    {
        return NULL;
    }
    return &builder->control_stack[builder->control_depth - 1];
}

CB_ControlEntry *cb_pop_control(CodeBuilder *builder, CB_ControlKind expected)
{
    if (builder->control_depth == 0)
    {
        fprintf(stderr, "control stack underflow\n");
        exit(1);
    }

    CB_ControlEntry *entry = &builder->control_stack[builder->control_depth - 1];
    if (entry->kind != expected)
    {
        fprintf(stderr, "control stack mismatch: expected %d, got %d\n",
                expected, entry->kind);
        exit(1);
    }

    builder->control_depth--;
    return entry;
}

CB_ControlEntry *cb_find_loop_or_switch(CodeBuilder *builder)
{
    for (int32_t i = (int32_t)builder->control_depth - 1; i >= 0; --i)
    {
        CB_ControlEntry *entry = &builder->control_stack[i];
        if (entry->kind == CB_CONTROL_LOOP || entry->kind == CB_CONTROL_SWITCH)
        {
            return entry;
        }
    }
    return NULL;
}

CB_ControlEntry *cb_find_loop(CodeBuilder *builder)
{
    for (int32_t i = (int32_t)builder->control_depth - 1; i >= 0; --i)
    {
        CB_ControlEntry *entry = &builder->control_stack[i];
        if (entry->kind == CB_CONTROL_LOOP)
        {
            return entry;
        }
    }
    return NULL;
}

/* ============================================================
 * High-Level Control Flow API - If Statement
 * ============================================================ */

void codebuilder_begin_if(CodeBuilder *builder)
{
    if (!builder)
    {
        return;
    }

    CB_ControlEntry *entry = cb_push_control(builder, CB_CONTROL_IF);

    entry->u.if_ctx.then_label = codebuilder_create_label(builder);
    entry->u.if_ctx.else_label = codebuilder_create_label(builder);
    entry->u.if_ctx.end_label = codebuilder_create_label(builder);

    /* Note: jump_only marking is no longer needed with alive-flag approach */

    entry->u.if_ctx.has_else = false;
    entry->u.if_ctx.in_then = false;
    entry->u.if_ctx.in_else = false;
}

void codebuilder_if_then(CodeBuilder *builder)
{
    if (!builder)
    {
        return;
    }

    CB_ControlEntry *entry = cb_top_control(builder);
    if (!entry || entry->kind != CB_CONTROL_IF)
    {
        fprintf(stderr, "codebuilder_if_then: not in if context\n");
        exit(1);
    }

    /* Condition is on stack - jump to else/end if false (0) */
    /* Frame is automatically saved to else_label by jump */
    codebuilder_jump_if_not(builder, entry->u.if_ctx.else_label);

    /* Place then label */
    codebuilder_place_label(builder, entry->u.if_ctx.then_label);
    entry->u.if_ctx.in_then = true;
}

void codebuilder_if_else(CodeBuilder *builder)
{
    if (!builder)
    {
        return;
    }

    CB_ControlEntry *entry = cb_top_control(builder);
    if (!entry || entry->kind != CB_CONTROL_IF)
    {
        fprintf(stderr, "codebuilder_if_else: not in if context\n");
        exit(1);
    }

    /* Jump from end of then to end of if */
    codebuilder_jump(builder, entry->u.if_ctx.end_label);

    /* Place else label - frame is automatically restored from saved state */
    codebuilder_place_label(builder, entry->u.if_ctx.else_label);
    entry->u.if_ctx.has_else = true;
    entry->u.if_ctx.in_then = false;
    entry->u.if_ctx.in_else = true;
}

void codebuilder_end_if(CodeBuilder *builder)
{
    if (!builder)
    {
        return;
    }

    CB_ControlEntry *entry = cb_pop_control(builder, CB_CONTROL_IF);

    /* If no else, place else label here (frame auto-restored) */
    if (!entry->u.if_ctx.has_else)
    {
        codebuilder_place_label(builder, entry->u.if_ctx.else_label);
    }

    /* Place end label */
    codebuilder_place_label(builder, entry->u.if_ctx.end_label);
}

/* ============================================================
 * High-Level Control Flow API - While Loop
 * ============================================================ */

void codebuilder_begin_while(CodeBuilder *builder)
{
    if (!builder)
    {
        return;
    }

    CB_ControlEntry *entry = cb_push_control(builder, CB_CONTROL_LOOP);

    entry->u.loop_ctx.start_label = codebuilder_create_label(builder);
    entry->u.loop_ctx.cond_label = entry->u.loop_ctx.start_label; /* While: cond at start */
    entry->u.loop_ctx.body_label = codebuilder_create_label(builder);
    entry->u.loop_ctx.post_label = NULL;
    entry->u.loop_ctx.end_label = codebuilder_create_label(builder);
    entry->u.loop_ctx.continue_label = entry->u.loop_ctx.cond_label;
    entry->u.loop_ctx.is_do_while = false;
    entry->u.loop_ctx.has_post = false;

    /* Mark as loop header for StackMap */
    codebuilder_mark_loop_header(builder, entry->u.loop_ctx.cond_label);

    /* Place condition label at loop start */
    codebuilder_place_label(builder, entry->u.loop_ctx.cond_label);
}

void codebuilder_while_body(CodeBuilder *builder)
{
    if (!builder)
    {
        return;
    }

    CB_ControlEntry *entry = cb_top_control(builder);
    if (!entry || entry->kind != CB_CONTROL_LOOP)
    {
        fprintf(stderr, "codebuilder_while_body: not in loop context\n");
        exit(1);
    }

    /* Condition is on stack - jump to end if false */
    codebuilder_jump_if_not(builder, entry->u.loop_ctx.end_label);

    /* Place body label */
    codebuilder_place_label(builder, entry->u.loop_ctx.body_label);
}

void codebuilder_end_while(CodeBuilder *builder)
{
    if (!builder)
    {
        return;
    }

    CB_ControlEntry *entry = cb_pop_control(builder, CB_CONTROL_LOOP);

    /* Jump back to condition */
    codebuilder_jump(builder, entry->u.loop_ctx.cond_label);

    /* Place end label */
    codebuilder_place_label(builder, entry->u.loop_ctx.end_label);
}

/* ============================================================
 * High-Level Control Flow API - Do-While Loop
 * ============================================================ */

void codebuilder_begin_do_while(CodeBuilder *builder)
{
    if (!builder)
    {
        return;
    }

    CB_ControlEntry *entry = cb_push_control(builder, CB_CONTROL_LOOP);

    entry->u.loop_ctx.start_label = codebuilder_create_label(builder);
    entry->u.loop_ctx.cond_label = codebuilder_create_label(builder);
    entry->u.loop_ctx.body_label = entry->u.loop_ctx.start_label; /* Do-while: body at start */
    entry->u.loop_ctx.post_label = NULL;
    entry->u.loop_ctx.end_label = codebuilder_create_label(builder);
    entry->u.loop_ctx.continue_label = entry->u.loop_ctx.cond_label;
    entry->u.loop_ctx.is_do_while = true;
    entry->u.loop_ctx.has_post = false;

    /* Mark body as loop header for backward branch */
    codebuilder_mark_loop_header(builder, entry->u.loop_ctx.body_label);

    /* Place body label at loop start */
    codebuilder_place_label(builder, entry->u.loop_ctx.body_label);

    /* Save current frame to cond_label for dead code recovery
     * (e.g., do { goto label; } while(0) patterns from bison) */
    cb_copy_frame(entry->u.loop_ctx.cond_label->frame, builder->frame);
    entry->u.loop_ctx.cond_label->frame_saved = true;
    /* Mark as jump target so StackMapTable entry is generated */
    entry->u.loop_ctx.cond_label->is_jump_target = true;
}

void codebuilder_do_while_cond(CodeBuilder *builder)
{
    if (!builder)
    {
        return;
    }

    CB_ControlEntry *entry = cb_top_control(builder);
    if (!entry || entry->kind != CB_CONTROL_LOOP)
    {
        fprintf(stderr, "codebuilder_do_while_cond: not in loop context\n");
        exit(1);
    }

    /* Place condition label */
    codebuilder_place_label(builder, entry->u.loop_ctx.cond_label);
}

void codebuilder_end_do_while(CodeBuilder *builder)
{
    if (!builder)
    {
        return;
    }

    CB_ControlEntry *entry = cb_pop_control(builder, CB_CONTROL_LOOP);

    /* Condition is on stack - jump to body if true */
    codebuilder_jump_if(builder, entry->u.loop_ctx.body_label);

    /* Place end label */
    codebuilder_place_label(builder, entry->u.loop_ctx.end_label);
}

/* ============================================================
 * High-Level Control Flow API - For Loop
 * ============================================================ */

void codebuilder_begin_for(CodeBuilder *builder)
{
    if (!builder)
    {
        return;
    }

    CB_ControlEntry *entry = cb_push_control(builder, CB_CONTROL_LOOP);

    entry->u.loop_ctx.start_label = codebuilder_create_label(builder);
    entry->u.loop_ctx.cond_label = codebuilder_create_label(builder);
    entry->u.loop_ctx.body_label = codebuilder_create_label(builder);
    entry->u.loop_ctx.post_label = codebuilder_create_label(builder);
    entry->u.loop_ctx.end_label = codebuilder_create_label(builder);
    entry->u.loop_ctx.continue_label = entry->u.loop_ctx.post_label; /* For: continue goes to post */
    entry->u.loop_ctx.is_do_while = false;
    entry->u.loop_ctx.has_post = false;

    /* Mark condition as loop header */
    codebuilder_mark_loop_header(builder, entry->u.loop_ctx.cond_label);

    /* Init is generated here (before for_cond is called) */
}

void codebuilder_for_cond(CodeBuilder *builder)
{
    if (!builder)
    {
        return;
    }

    CB_ControlEntry *entry = cb_top_control(builder);
    if (!entry || entry->kind != CB_CONTROL_LOOP)
    {
        fprintf(stderr, "codebuilder_for_cond: not in loop context\n");
        exit(1);
    }

    /* Place condition label */
    codebuilder_place_label(builder, entry->u.loop_ctx.cond_label);
}

void codebuilder_for_body(CodeBuilder *builder)
{
    if (!builder)
    {
        return;
    }

    CB_ControlEntry *entry = cb_top_control(builder);
    if (!entry || entry->kind != CB_CONTROL_LOOP)
    {
        fprintf(stderr, "codebuilder_for_body: not in loop context\n");
        exit(1);
    }

    /* Check if there's a condition on stack */
    if (builder->frame->stack_count > 0)
    {
        /* Condition is on stack - jump to end if false */
        codebuilder_jump_if_not(builder, entry->u.loop_ctx.end_label);
    }

    /* Place body label */
    codebuilder_place_label(builder, entry->u.loop_ctx.body_label);
}

void codebuilder_for_post(CodeBuilder *builder)
{
    if (!builder)
    {
        return;
    }

    CB_ControlEntry *entry = cb_top_control(builder);
    if (!entry || entry->kind != CB_CONTROL_LOOP)
    {
        fprintf(stderr, "codebuilder_for_post: not in loop context\n");
        exit(1);
    }

    /* Place post label */
    codebuilder_place_label(builder, entry->u.loop_ctx.post_label);
    entry->u.loop_ctx.has_post = true;
}

void codebuilder_end_for(CodeBuilder *builder)
{
    if (!builder)
    {
        return;
    }

    CB_ControlEntry *entry = cb_pop_control(builder, CB_CONTROL_LOOP);

    /* Jump back to condition */
    codebuilder_jump(builder, entry->u.loop_ctx.cond_label);

    /* Place end label */
    codebuilder_place_label(builder, entry->u.loop_ctx.end_label);
}

/* ============================================================
 * High-Level Control Flow API - Break/Continue
 * ============================================================ */

void codebuilder_emit_break(CodeBuilder *builder)
{
    if (!builder)
    {
        return;
    }

    CB_ControlEntry *entry = cb_find_loop_or_switch(builder);
    if (!entry)
    {
        fprintf(stderr, "break outside loop or switch\n");
        exit(1);
    }

    CB_Label *target = NULL;
    if (entry->kind == CB_CONTROL_LOOP)
    {
        target = entry->u.loop_ctx.end_label;
    }
    else if (entry->kind == CB_CONTROL_SWITCH)
    {
        target = entry->u.switch_ctx.end_label;
    }

    if (target)
    {
        codebuilder_set_jump_context(builder, "break");
        codebuilder_jump(builder, target);
    }
}

void codebuilder_emit_continue(CodeBuilder *builder)
{
    if (!builder)
    {
        return;
    }

    CB_ControlEntry *entry = cb_find_loop(builder);
    if (!entry)
    {
        fprintf(stderr, "continue outside loop\n");
        exit(1);
    }

    CB_Label *target = entry->u.loop_ctx.continue_label;
    if (target)
    {
        codebuilder_set_jump_context(builder, "continue");
        codebuilder_jump(builder, target);
    }
}

/* ============================================================
 * High-Level Control Flow API - Switch Statement
 * ============================================================ */

void codebuilder_begin_switch(CodeBuilder *builder)
{
    if (!builder)
    {
        return;
    }

    CB_ControlEntry *entry = cb_push_control(builder, CB_CONTROL_SWITCH);

    entry->u.switch_ctx.dispatch_label = codebuilder_create_label(builder);
    entry->u.switch_ctx.default_label = codebuilder_create_label(builder);
    entry->u.switch_ctx.end_label = codebuilder_create_label(builder);
    entry->u.switch_ctx.cases = NULL;
    entry->u.switch_ctx.case_count = 0;
    entry->u.switch_ctx.case_capacity = 0;
    entry->u.switch_ctx.expr_local = 0;
    entry->u.switch_ctx.has_default = false;
}

void codebuilder_switch_dispatch(CodeBuilder *builder, int expr_local)
{
    if (!builder)
    {
        return;
    }

    CB_ControlEntry *entry = cb_top_control(builder);
    if (!entry || entry->kind != CB_CONTROL_SWITCH)
    {
        fprintf(stderr, "codebuilder_switch_dispatch: not in switch context\n");
        exit(1);
    }

    entry->u.switch_ctx.expr_local = expr_local;

    /* Jump to dispatch table (generated at end) */
    codebuilder_jump(builder, entry->u.switch_ctx.dispatch_label);
}

static void ensure_switch_case_capacity(CB_ControlEntry *entry)
{
    if (entry->u.switch_ctx.case_count < entry->u.switch_ctx.case_capacity)
    {
        return;
    }

    int old_capacity = entry->u.switch_ctx.case_capacity;
    int new_capacity = old_capacity == 0 ? 8 : old_capacity * 2;
    CB_SwitchCase *new_cases = (CB_SwitchCase *)calloc(
        new_capacity, sizeof(CB_SwitchCase));
    for (int i = 0; i < entry->u.switch_ctx.case_count; i++)
    {
        new_cases[i] = entry->u.switch_ctx.cases[i];
    }
    entry->u.switch_ctx.cases = new_cases;
    entry->u.switch_ctx.case_capacity = new_capacity;
}

void codebuilder_switch_case(CodeBuilder *builder, int32_t value)
{
    if (!builder)
    {
        return;
    }

    CB_ControlEntry *entry = cb_top_control(builder);
    if (!entry || entry->kind != CB_CONTROL_SWITCH)
    {
        fprintf(stderr, "codebuilder_switch_case: not in switch context\n");
        exit(1);
    }

    /* Create and place case label */
    CB_Label *case_label = codebuilder_create_label(builder);
    codebuilder_place_label(builder, case_label);

    /* Record case */
    ensure_switch_case_capacity(entry);
    entry->u.switch_ctx.cases[entry->u.switch_ctx.case_count].value = value;
    entry->u.switch_ctx.cases[entry->u.switch_ctx.case_count].label = case_label;
    entry->u.switch_ctx.case_count++;
}

void codebuilder_switch_default(CodeBuilder *builder)
{
    if (!builder)
    {
        return;
    }

    CB_ControlEntry *entry = cb_top_control(builder);
    if (!entry || entry->kind != CB_CONTROL_SWITCH)
    {
        fprintf(stderr, "codebuilder_switch_default: not in switch context\n");
        exit(1);
    }

    /* Place default label */
    codebuilder_place_label(builder, entry->u.switch_ctx.default_label);
    entry->u.switch_ctx.has_default = true;
}

int codebuilder_switch_expr_local(CodeBuilder *builder)
{
    if (!builder)
    {
        return 0;
    }

    CB_ControlEntry *entry = cb_top_control(builder);
    if (!entry || entry->kind != CB_CONTROL_SWITCH)
    {
        return 0;
    }

    return entry->u.switch_ctx.expr_local;
}

/* Insertion sort for switch cases by value */
static void sort_switch_cases(CB_SwitchCase *cases, int count)
{
    for (int i = 1; i < count; i++)
    {
        CB_SwitchCase tmp = cases[i];
        int j = i;
        while (j > 0 && cases[j - 1].value > tmp.value)
        {
            cases[j] = cases[j - 1];
            j--;
        }
        cases[j] = tmp;
    }
}

void codebuilder_end_switch(CodeBuilder *builder)
{
    if (!builder)
    {
        return;
    }

    CB_ControlEntry *entry = cb_pop_control(builder, CB_CONTROL_SWITCH);

    /* Jump to end from last case (fall through to dispatch) */
    codebuilder_jump(builder, entry->u.switch_ctx.end_label);

    /* Place dispatch label */
    codebuilder_place_label(builder, entry->u.switch_ctx.dispatch_label);

    /* If no default, default goes to end */
    if (!entry->u.switch_ctx.has_default)
    {
        entry->u.switch_ctx.default_label = entry->u.switch_ctx.end_label;
    }

    /* Sort cases by value */
    if (entry->u.switch_ctx.case_count > 1)
    {
        sort_switch_cases(entry->u.switch_ctx.cases, entry->u.switch_ctx.case_count);
    }

    /* Generate dispatch code */
    codebuilder_build_iload(builder, entry->u.switch_ctx.expr_local);

    if (entry->u.switch_ctx.case_count == 0)
    {
        /* No cases - just pop and jump to default */
        codebuilder_build_pop(builder);
        codebuilder_jump(builder, entry->u.switch_ctx.default_label);
    }
    else if (entry->u.switch_ctx.case_count < 3)
    {
        /* Few cases - use if-else chain */
        for (int i = 0; i < entry->u.switch_ctx.case_count; ++i)
        {
            CB_SwitchCase c = entry->u.switch_ctx.cases[i];
            codebuilder_build_dup(builder);
            codebuilder_build_iconst(builder, c.value);
            codebuilder_build_isub(builder);
            codebuilder_jump_if_not(builder, c.label);
        }
        codebuilder_build_pop(builder);
        codebuilder_jump(builder, entry->u.switch_ctx.default_label);
    }
    else
    {
        /* 3+ cases - use tableswitch or lookupswitch */
        int32_t low = entry->u.switch_ctx.cases[0].value;
        int32_t high = entry->u.switch_ctx.cases[entry->u.switch_ctx.case_count - 1].value;

        if (codebuilder_should_use_tableswitch(entry->u.switch_ctx.case_count, low, high))
        {
            /* Build jump table for tableswitch */
            int32_t table_size = high - low + 1;
            CB_Label **jump_table = (CB_Label **)calloc(table_size, sizeof(CB_Label *));

            /* Initialize all entries to default */
            for (int32_t i = 0; i < table_size; i++)
            {
                jump_table[i] = entry->u.switch_ctx.default_label;
            }

            /* Fill in actual case targets */
            for (int i = 0; i < entry->u.switch_ctx.case_count; i++)
            {
                int32_t idx = entry->u.switch_ctx.cases[i].value - low;
                jump_table[idx] = entry->u.switch_ctx.cases[i].label;
            }

            codebuilder_build_tableswitch(builder, entry->u.switch_ctx.default_label,
                                          low, high, jump_table);
        }
        else
        {
            /* Build arrays for lookupswitch */
            int32_t *keys = (int32_t *)calloc(entry->u.switch_ctx.case_count, sizeof(int32_t));
            CB_Label **targets = (CB_Label **)calloc(entry->u.switch_ctx.case_count, sizeof(CB_Label *));

            for (int i = 0; i < entry->u.switch_ctx.case_count; i++)
            {
                keys[i] = entry->u.switch_ctx.cases[i].value;
                targets[i] = entry->u.switch_ctx.cases[i].label;
            }

            codebuilder_build_lookupswitch(builder, entry->u.switch_ctx.default_label,
                                           entry->u.switch_ctx.case_count, keys, targets);
        }
    }

    /* Place end label */
    codebuilder_place_label(builder, entry->u.switch_ctx.end_label);

    /* Free case array */
    if (entry->u.switch_ctx.cases)
    {
        free(entry->u.switch_ctx.cases);
        entry->u.switch_ctx.cases = NULL;
    }
}

/* ============================================================
 * Visitor-Friendly Control Stack API
 * ============================================================ */

CB_ControlEntry *codebuilder_push_loop_raw(CodeBuilder *builder)
{
    if (!builder)
    {
        return NULL;
    }

    CB_ControlEntry *entry = cb_push_control(builder, CB_CONTROL_LOOP);

    /* Initialize all labels to NULL - caller must set them */
    entry->u.loop_ctx.start_label = NULL;
    entry->u.loop_ctx.cond_label = NULL;
    entry->u.loop_ctx.body_label = NULL;
    entry->u.loop_ctx.post_label = NULL;
    entry->u.loop_ctx.end_label = NULL;
    entry->u.loop_ctx.continue_label = NULL;
    entry->u.loop_ctx.is_do_while = false;
    entry->u.loop_ctx.has_post = false;

    return entry;
}

void codebuilder_pop_loop_raw(CodeBuilder *builder)
{
    if (!builder)
    {
        return;
    }

    cb_pop_control(builder, CB_CONTROL_LOOP);
}

CB_ControlEntry *codebuilder_current_loop(CodeBuilder *builder)
{
    if (!builder)
    {
        return NULL;
    }

    return cb_find_loop(builder);
}

CB_ControlEntry *codebuilder_push_switch_raw(CodeBuilder *builder)
{
    if (!builder)
    {
        return NULL;
    }

    CB_ControlEntry *entry = cb_push_control(builder, CB_CONTROL_SWITCH);

    /* Initialize all fields - caller must set labels */
    entry->u.switch_ctx.dispatch_label = NULL;
    entry->u.switch_ctx.default_label = NULL;
    entry->u.switch_ctx.end_label = NULL;
    entry->u.switch_ctx.cases = NULL;
    entry->u.switch_ctx.case_count = 0;
    entry->u.switch_ctx.case_capacity = 0;
    entry->u.switch_ctx.expr_local = 0;
    entry->u.switch_ctx.has_default = false;
    entry->u.switch_ctx.entry_frame = NULL;

    return entry;
}

void codebuilder_pop_switch_raw(CodeBuilder *builder)
{
    if (!builder)
    {
        return;
    }

    /* Free cases array before popping */
    CB_ControlEntry *entry = codebuilder_current_switch(builder);
    if (entry && entry->u.switch_ctx.cases)
    {
        free(entry->u.switch_ctx.cases);
        entry->u.switch_ctx.cases = NULL;
        entry->u.switch_ctx.case_count = 0;
        entry->u.switch_ctx.case_capacity = 0;
    }

    cb_pop_control(builder, CB_CONTROL_SWITCH);
}

CB_ControlEntry *codebuilder_current_switch(CodeBuilder *builder)
{
    if (!builder)
    {
        return NULL;
    }

    /* Find the innermost switch context */
    for (int32_t i = (int32_t)builder->control_depth - 1; i >= 0; --i)
    {
        CB_ControlEntry *entry = &builder->control_stack[i];
        if (entry->kind == CB_CONTROL_SWITCH)
        {
            return entry;
        }
    }

    return NULL;
}

void codebuilder_switch_add_case(CodeBuilder *builder, int32_t value, CB_Label *label)
{
    if (!builder)
    {
        return;
    }

    CB_ControlEntry *entry = codebuilder_current_switch(builder);
    if (!entry)
    {
        fprintf(stderr, "codebuilder_switch_add_case: not in switch context\n");
        exit(1);
    }

    /* Ensure capacity */
    if (entry->u.switch_ctx.case_count >= entry->u.switch_ctx.case_capacity)
    {
        int new_cap = entry->u.switch_ctx.case_capacity ? entry->u.switch_ctx.case_capacity * 2 : 8;
        CB_SwitchCase *new_cases = (CB_SwitchCase *)calloc(
            new_cap, sizeof(CB_SwitchCase));
        for (int i = 0; i < entry->u.switch_ctx.case_count; i++)
        {
            new_cases[i] = entry->u.switch_ctx.cases[i];
        }
        entry->u.switch_ctx.cases = new_cases;
        entry->u.switch_ctx.case_capacity = new_cap;
    }

    entry->u.switch_ctx.cases[entry->u.switch_ctx.case_count].value = value;
    entry->u.switch_ctx.cases[entry->u.switch_ctx.case_count].label = label;
    entry->u.switch_ctx.case_count++;
}

/* ============================================================
 * Switch Instruction Generation (tableswitch/lookupswitch)
 * ============================================================ */

bool codebuilder_should_use_tableswitch(int32_t nlabels, int32_t low, int32_t high)
{
    /* Javac cost model from Gen.java:
     * table_space_cost = 4 + (hi - lo + 1)
     * table_time_cost = 3
     * lookup_space_cost = 3 + 2 * nlabels
     * lookup_time_cost = nlabels
     *
     * Use tableswitch if:
     *   nlabels > 0 &&
     *   table_space_cost + 3 * table_time_cost <= lookup_space_cost + 3 * lookup_time_cost
     */
    if (nlabels <= 0)
    {
        return false;
    }

    long table_space_cost = 4 + ((long)high - low + 1);
    long table_time_cost = 3;
    long lookup_space_cost = 3 + 2 * (long)nlabels;
    long lookup_time_cost = nlabels;

    return table_space_cost + 3 * table_time_cost <=
           lookup_space_cost + 3 * lookup_time_cost;
}

void codebuilder_build_tableswitch(CodeBuilder *builder,
                                   CB_Label *default_label,
                                   int32_t low, int32_t high,
                                   CB_Label **jump_table)
{
    if (!builder || !default_label || !jump_table)
    {
        return;
    }

    /* All labels must be placed */
    if (!default_label->is_placed)
    {
        fprintf(stderr, "codebuilder_build_tableswitch: default label not placed\n");
        exit(1);
    }

    int32_t table_size = high - low + 1;
    for (int32_t i = 0; i < table_size; i++)
    {
        if (!jump_table[i]->is_placed)
        {
            fprintf(stderr, "codebuilder_build_tableswitch: jump_table[%d] label not placed\n", i);
            exit(1);
        }
    }

    /* Record the PC where tableswitch opcode will be emitted */
    int switch_pc = codebuilder_current_pc(builder);

    /* Calculate offsets relative to switch_pc */
    int32_t default_offset = (int32_t)default_label->pc - (int32_t)switch_pc;

    int32_t *offsets = (int32_t *)calloc(table_size, sizeof(int32_t));
    for (int32_t i = 0; i < table_size; i++)
    {
        offsets[i] = (int32_t)jump_table[i]->pc - (int32_t)switch_pc;
    }

    /* Emit the instruction */
    classfile_opcode_emit_tableswitch(builder->method, default_offset, low, high, offsets);

    /* Pop the switch value from stack */
    cb_pop(builder);

    /* Record branch targets for StackMapTable.
     * Use the label's saved frame if available, since the label was placed
     * with a specific frame state that may differ from current builder->frame.
     * This is especially important when multiple switch statements share labels
     * or when locals are initialized inside case blocks. */
    if (!default_label->frame_recorded)
    {
        if (default_label->frame_saved)
        {
            codebuilder_record_branch_target_with_frame(builder, default_label->pc, default_label->frame);
        }
        else
        {
            codebuilder_record_branch_target(builder, default_label->pc);
        }
        default_label->frame_recorded = true;
    }
    for (int32_t i = 0; i < table_size; i++)
    {
        if (!jump_table[i]->frame_recorded)
        {
            if (jump_table[i]->frame_saved)
            {
                codebuilder_record_branch_target_with_frame(builder, jump_table[i]->pc, jump_table[i]->frame);
            }
            else
            {
                codebuilder_record_branch_target(builder, jump_table[i]->pc);
            }
            jump_table[i]->frame_recorded = true;
        }
    }

    /* tableswitch is unconditional - code after is unreachable */
    codebuilder_mark_dead(builder);
}

void codebuilder_build_lookupswitch(CodeBuilder *builder,
                                    CB_Label *default_label,
                                    int32_t npairs,
                                    const int32_t *keys,
                                    CB_Label **targets)
{
    if (!builder || !default_label)
    {
        return;
    }

    /* All labels must be placed */
    if (!default_label->is_placed)
    {
        fprintf(stderr, "codebuilder_build_lookupswitch: default label not placed\n");
        exit(1);
    }

    for (int32_t i = 0; i < npairs; i++)
    {
        if (!targets[i]->is_placed)
        {
            fprintf(stderr, "codebuilder_build_lookupswitch: targets[%d] label not placed\n", i);
            exit(1);
        }
    }

    /* Record the PC where lookupswitch opcode will be emitted */
    int switch_pc = codebuilder_current_pc(builder);

    /* Calculate offsets relative to switch_pc */
    int32_t default_offset = (int32_t)default_label->pc - (int32_t)switch_pc;

    int32_t *offsets = (int32_t *)calloc(npairs, sizeof(int32_t));
    for (int32_t i = 0; i < npairs; i++)
    {
        offsets[i] = (int32_t)targets[i]->pc - (int32_t)switch_pc;
    }

    /* Emit the instruction */
    classfile_opcode_emit_lookupswitch(builder->method, default_offset, npairs, keys, offsets);

    /* Pop the switch value from stack */
    cb_pop(builder);

    /* Record branch targets for StackMapTable.
     * Use the label's saved frame if available, since the label was placed
     * with a specific frame state that may differ from current builder->frame. */
    if (!default_label->frame_recorded)
    {
        if (default_label->frame_saved)
        {
            codebuilder_record_branch_target_with_frame(builder, default_label->pc, default_label->frame);
        }
        else
        {
            codebuilder_record_branch_target(builder, default_label->pc);
        }
        default_label->frame_recorded = true;
    }
    for (int32_t i = 0; i < npairs; i++)
    {
        if (!targets[i]->frame_recorded)
        {
            if (targets[i]->frame_saved)
            {
                codebuilder_record_branch_target_with_frame(builder, targets[i]->pc, targets[i]->frame);
            }
            else
            {
                codebuilder_record_branch_target(builder, targets[i]->pc);
            }
            targets[i]->frame_recorded = true;
        }
    }

    free(offsets);

    /* lookupswitch is unconditional - code after is unreachable */
    codebuilder_mark_dead(builder);
}
