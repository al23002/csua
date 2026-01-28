#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codegenvisitor_stmt_switch_jump.h"
#include "codegenvisitor.h"
#include "codegenvisitor_util.h"
#include "codegenvisitor_stmt_util.h"
#include "codebuilder_ptr.h"
#include "codebuilder_control.h"
#include "codebuilder_internal.h"
#include "codebuilder_label.h"
#include "codebuilder_core.h"
#include "codebuilder_part1.h"
#include "codebuilder_part2.h"
#include "codebuilder_part3.h"
#include "codegen_symbols.h"
#include "codegen_constants.h"
#include "cminor_type.h"

void enter_switchstmt(Statement *stmt, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    handle_if_boundary(cg, stmt);
    handle_for_body_entry(cg, stmt);
    cg_begin_scope(cg, true);
    push_switch_context(cg, stmt);
}

void leave_switchstmt(Statement *stmt, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;

    /* Get CodeBuilder's switch context before popping */
    CB_ControlEntry *entry = codebuilder_current_switch(cg->builder);
    if (!entry)
    {
        fprintf(stderr, "no switch context in CodeBuilder\n");
        exit(1);
    }

    CodegenSwitchContext ctx = pop_switch_context(cg, stmt);

    /* Dead code path - just place labels and exit */
    if (!ctx.has_expr_local)
    {
        /* Place dispatch_label and end_label without generating dispatch code */
        if (!entry->u.switch_ctx.dispatch_label->is_placed)
        {
            codebuilder_place_label(cg->builder, entry->u.switch_ctx.dispatch_label);
        }
        codebuilder_place_label(cg->builder, entry->u.switch_ctx.end_label);
        codebuilder_pop_switch_raw(cg->builder);
        cg_end_scope(cg, "switch statement");
        return;
    }

    if (!ctx.has_dispatch_goto)
    {
        fprintf(stderr, "switch dispatch setup incomplete\n");
        exit(1);
    }

    codebuilder_jump(cg->builder, entry->u.switch_ctx.end_label);

    codebuilder_place_label(cg->builder, entry->u.switch_ctx.dispatch_label);

    /* Determine default target */
    CB_Label *default_target;
    if (entry->u.switch_ctx.default_label)
    {
        default_target = entry->u.switch_ctx.default_label;
    }
    else
    {
        /* No explicit default: create implicit default that jumps to end.
         * We need to place this label BEFORE tableswitch/lookupswitch
         * because those instructions require all target labels to be placed. */
        default_target = codebuilder_create_label(cg->builder);

        /* Skip over implicit default handling */
        CB_Label *skip_implicit_default = codebuilder_create_label(cg->builder);
        codebuilder_jump(cg->builder, skip_implicit_default);

        /* Place implicit default - switch will jump here for unmatched values */
        codebuilder_place_label(cg->builder, default_target);
        codebuilder_jump(cg->builder, entry->u.switch_ctx.end_label);

        /* Continue with actual dispatch */
        codebuilder_place_label(cg->builder, skip_implicit_default);
    }

    if (entry->u.switch_ctx.case_count == 0)
    {
        /* No cases - jump to default */
        codebuilder_jump(cg->builder, default_target);
    }
    else if (entry->u.switch_ctx.case_count < 3)
    {
        /* Few cases - use if-else chain */
        for (int i = 0; i < entry->u.switch_ctx.case_count; ++i)
        {
            codebuilder_build_iload(cg->builder, entry->u.switch_ctx.expr_local);
            codebuilder_build_iconst(cg->builder, entry->u.switch_ctx.cases[i].value);
            codebuilder_jump_if_icmp(cg->builder, ICMP_EQ, entry->u.switch_ctx.cases[i].label);
        }
        codebuilder_jump(cg->builder, default_target);
    }
    else
    {
        /* 3+ cases - use tableswitch or lookupswitch */
        /* Sort cases by value first */
        for (int i = 0; i < entry->u.switch_ctx.case_count - 1; i++)
        {
            for (int j = i + 1; j < entry->u.switch_ctx.case_count; j++)
            {
                if (entry->u.switch_ctx.cases[i].value > entry->u.switch_ctx.cases[j].value)
                {
                    CB_SwitchCase tmp = entry->u.switch_ctx.cases[i];
                    entry->u.switch_ctx.cases[i] = entry->u.switch_ctx.cases[j];
                    entry->u.switch_ctx.cases[j] = tmp;
                }
            }
        }

        int32_t low = entry->u.switch_ctx.cases[0].value;
        int32_t high = entry->u.switch_ctx.cases[entry->u.switch_ctx.case_count - 1].value;

        /* Load switch expression as int */
        codebuilder_build_iload(cg->builder, entry->u.switch_ctx.expr_local);

        if (codebuilder_should_use_tableswitch(entry->u.switch_ctx.case_count, low, high))
        {
            /* Build jump table for tableswitch */
            int32_t table_size = high - low + 1;
            CB_Label **jump_table = (CB_Label **)calloc(table_size, sizeof(CB_Label *));

            /* Initialize all entries to default */
            for (int32_t i = 0; i < table_size; i++)
            {
                jump_table[i] = default_target;
            }

            /* Fill in actual case targets */
            for (int i = 0; i < entry->u.switch_ctx.case_count; i++)
            {
                int32_t idx = entry->u.switch_ctx.cases[i].value - low;
                jump_table[idx] = entry->u.switch_ctx.cases[i].label;
            }

            codebuilder_build_tableswitch(cg->builder, default_target,
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

            codebuilder_build_lookupswitch(cg->builder, default_target,
                                           entry->u.switch_ctx.case_count, keys, targets);
        }
    }

    codebuilder_place_label(cg->builder, entry->u.switch_ctx.end_label);

    /* Pop CodeBuilder's switch context after using its data */
    codebuilder_pop_switch_raw(cg->builder);

    cg_end_scope(cg, "switch statement");
}

void leave_returnstmt(Statement *stmt, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    /* Get return type from function declaration
     * cminor_main now returns int (synthetic main wrapper handles the conversion) */
    TypeSpecifier *return_type = NULL;
    if (cg->current_function)
    {
        return_type = cg->current_function->type;
    }
    if (!return_type || cs_type_is_void(return_type))
    {
        if (cg->builder->frame->stack_count > 0)
        {
            codebuilder_build_pop(cg->builder);
        }
        codebuilder_build_return(cg->builder);
    }
    else
    {
        if (cg->builder->frame->stack_count == 0)
        {
            if (cs_type_is_pointer(return_type))
            {
                /* Generate null pointer wrapper: __ptr(null, 0) */
                codebuilder_build_aconst_null(cg->builder);
                codebuilder_build_iconst(cg->builder, 0);
                cg_emit_ptr_create(cg, return_type);
            }
            else if (cs_type_is_aggregate(return_type))
            {
                codebuilder_build_aconst_null(cg->builder);
            }
            else if (cs_type_is_double_exact(return_type))
            {
                codebuilder_build_dconst(cg->builder, 0.0);
            }
            else if (cs_type_is_float_exact(return_type))
            {
                codebuilder_build_fconst(cg->builder, 0.0f);
            }
            else if (cs_type_is_long_exact(return_type))
            {
                codebuilder_build_lconst(cg->builder, 0);
            }
            else
            {
                codebuilder_build_iconst(cg->builder, 0);
            }
        }

        if (cs_type_is_aggregate(return_type) || cs_type_is_pointer(return_type) ||
            cs_type_is_array(return_type))
        {
            /* Deep copy struct before returning (C value semantics) */
            if (cs_type_is_named(return_type) &&
                cs_type_is_basic_struct_or_union(return_type))
            {
                cg_emit_struct_deep_copy(cg, return_type);
            }
            codebuilder_build_areturn(cg->builder);
        }
        else if (cs_type_is_double_exact(return_type))
        {
            codebuilder_build_dreturn(cg->builder);
        }
        else if (cs_type_is_float_exact(return_type))
        {
            codebuilder_build_freturn(cg->builder);
        }
        else if (cs_type_is_long_exact(return_type))
        {
            codebuilder_build_lreturn(cg->builder);
        }
        else if (cs_type_is_int_exact(return_type) || cs_type_is_short_exact(return_type) ||
                 cs_type_is_char_exact(return_type) || cs_type_is_bool(return_type) ||
                 cs_type_is_enum(return_type))
        {
            codebuilder_build_ireturn(cg->builder);
        }
        else
        {
            /* Named types (typedefs) that are not primitives use areturn */
            codebuilder_build_areturn(cg->builder);
        }
    }

    cg->ctx.has_return = true;
    /* No scope cleanup (block-level scoping) */
}

void leave_breakstmt(Statement *stmt, Visitor *visitor)
{
    (void)stmt;
    CodegenVisitor *cg = (CodegenVisitor *)visitor;

    /* Use CodeBuilder's break emission - it handles all the control stack logic */
    codebuilder_emit_break(cg->builder);
}

void leave_continuestmt(Statement *stmt, Visitor *visitor)
{
    (void)stmt;
    CodegenVisitor *cg = (CodegenVisitor *)visitor;

    /* Use CodeBuilder's continue emission - it handles all the control stack logic */
    codebuilder_emit_continue(cg->builder);
}

void enter_casestmt(Statement *stmt, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    cg_begin_scope(cg, false);

    if (cg->ctx.switch_depth == 0)
    {
        fprintf(stderr, "case used outside of switch\n");
        exit(1);
    }

    /* Get switch context to access entry_frame */
    CB_ControlEntry *entry = codebuilder_current_switch(cg->builder);
    if (!entry)
    {
        fprintf(stderr, "no switch context in CodeBuilder\n");
        exit(1);
    }

    /* Add case to CodeBuilder's switch context */
    CB_Label *case_block = codebuilder_create_label(cg->builder);

    /* Copy entry_frame to case label before placing.
     * This ensures that when the label is placed, the frame state is correct
     * and alive is restored to true (since frame_saved will be true). */
    if (entry->u.switch_ctx.entry_frame)
    {
        cb_copy_frame(case_block->frame, entry->u.switch_ctx.entry_frame);
        case_block->frame_saved = true;
    }

    codebuilder_place_label(cg->builder, case_block);
    codebuilder_switch_add_case(cg->builder,
                                eval_case_value(stmt->u.case_s.expression),
                                case_block);
}

void leave_casestmt(Statement *stmt, Visitor *visitor)
{
    (void)stmt;
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    cg_end_scope(cg, "case statement");
}

void enter_defaultstmt(Statement *stmt, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    cg_begin_scope(cg, false);

    if (cg->ctx.switch_depth == 0)
    {
        fprintf(stderr, "default used outside of switch\n");
        exit(1);
    }

    /* Set default label in CodeBuilder's switch context */
    CB_ControlEntry *entry = codebuilder_current_switch(cg->builder);
    if (!entry)
    {
        fprintf(stderr, "no switch context in CodeBuilder\n");
        exit(1);
    }

    if (entry->u.switch_ctx.default_label)
    {
        fprintf(stderr, "multiple default labels in switch\n");
        exit(1);
    }
    entry->u.switch_ctx.default_label = codebuilder_create_label(cg->builder);

    /* Copy entry_frame to default label before placing.
     * This ensures that when the label is placed, the frame state is correct
     * and alive is restored to true (since frame_saved will be true). */
    if (entry->u.switch_ctx.entry_frame)
    {
        cb_copy_frame(entry->u.switch_ctx.default_label->frame, entry->u.switch_ctx.entry_frame);
        entry->u.switch_ctx.default_label->frame_saved = true;
    }

    codebuilder_place_label(cg->builder, entry->u.switch_ctx.default_label);
}

void leave_defaultstmt(Statement *stmt, Visitor *visitor)
{
    (void)stmt;
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    cg_end_scope(cg, "default statement");
}

/* ========================================================================
 * Goto / Label Statement Handlers
 * ======================================================================== */

/* Helper: get or create a label by name (function-scoped) */
static CB_Label *cg_get_or_create_label(CodegenVisitor *cg, const char *name)
{
    /* Search existing labels */
    for (int i = 0; i < cg->ctx.label_count; i++)
    {
        if (strcmp(cg->ctx.label_names[i], name) == 0)
        {
            return cg->ctx.label_targets[i];
        }
    }

    /* Create new label */
    if (cg->ctx.label_count >= cg->ctx.label_capacity)
    {
        int new_capacity = cg->ctx.label_capacity == 0 ? 8 : cg->ctx.label_capacity * 2;
        char **new_names = calloc(new_capacity, sizeof(char *));
        CB_Label **new_targets = calloc(new_capacity, sizeof(CB_Label *));
        for (int i = 0; i < cg->ctx.label_count; i++)
        {
            new_names[i] = cg->ctx.label_names[i];
            new_targets[i] = cg->ctx.label_targets[i];
        }
        cg->ctx.label_names = new_names;
        cg->ctx.label_targets = new_targets;
        cg->ctx.label_capacity = new_capacity;
    }

    CB_Label *label = codebuilder_create_label(cg->builder);
    cg->ctx.label_names[cg->ctx.label_count] = (char *)name;
    cg->ctx.label_targets[cg->ctx.label_count] = label;
    cg->ctx.label_count++;

    return label;
}

void enter_labelstmt(Statement *stmt, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    handle_if_boundary(cg, stmt);
    handle_for_body_entry(cg, stmt);

    const char *label_name = stmt->u.label_s.label;
    CB_Label *label = cg_get_or_create_label(cg, label_name);

    /* Place the label at current position
     * Note: Label may already be placed if it was a forward reference,
     * but CodeBuilder will handle duplicate place_label gracefully */
    if (!label->is_placed)
    {
        codebuilder_place_label(cg->builder, label);
    }

    /*
     * Always mark code as alive after placing a label.
     * Even if currently dead, there may be a backward jump to this label.
     * The code following the label must be generated.
     */
    codebuilder_mark_alive(cg->builder);
}

void leave_labelstmt(Statement *stmt, Visitor *visitor)
{
    (void)stmt;
    (void)visitor;
    /* Label statement itself has no leave action - the labeled statement
     * is traversed as a child */
}

void leave_gotostmt(Statement *stmt, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;

    const char *label_name = stmt->u.goto_s.label;
    CB_Label *label = cg_get_or_create_label(cg, label_name);

    /* For backward jumps (label already placed), mark as loop header for StackMap */
    if (label->is_placed)
    {
        codebuilder_mark_loop_header(cg->builder, label);
    }
    else
    {
        /* Forward jump - mark as jump-only for StackMap frame recording */
        codebuilder_mark_jump_only(cg->builder, label);
    }

    /* Emit unconditional jump */
    codebuilder_set_jump_context(cg->builder, "goto");
    codebuilder_jump(cg->builder, label);
}
