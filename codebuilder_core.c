/*
 * CodeBuilder Core - CodeBuilder Lifecycle Management
 *
 * Handles:
 * - CodeBuilder creation and destruction
 * - Initial frame setup from method signature
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codebuilder_core.h"
#include "codebuilder_internal.h"
#include "codebuilder_types.h"

/* ============================================================
 * CodeBuilder Lifecycle
 * ============================================================ */

CodeBuilder *codebuilder_create(ConstantPoolBuilder *cp, MethodCode *method,
                                bool is_static, const char *class_name,
                                ParameterList *params, const char *method_name)
{
    CodeBuilder *builder = (CodeBuilder *)calloc(1, sizeof(CodeBuilder));
    builder->cp = cp;
    builder->method = method;

    builder->frame = cb_create_frame();
    builder->initial_frame = cb_create_frame();

    builder->branch_targets = NULL;
    builder->branch_target_count = 0;
    builder->branch_target_capacity = 0;

    builder->labels = NULL;
    builder->label_count = 0;
    builder->label_capacity = 0;

    builder->pending_jumps = NULL;
    builder->pending_jump_count = 0;
    builder->pending_jump_capacity = 0;

    builder->control_stack = NULL;
    builder->control_depth = 0;
    builder->control_capacity = 0;

    builder->max_stack = 0;
    builder->max_locals = 0;

    builder->alive = true;
    builder->block_depth = 0;
    builder->block_locals_base = (int *)calloc(CB_MAX_SCOPE_DEPTH, sizeof(int));
    builder->method_name = method_name;
    builder->class_name = class_name;

    /* Initialize locals from method signature */
    uint16_t local_index = 0;

    /* For instance methods, slot 0 is 'this' */
    if (!is_static && class_name)
    {
        CB_VerificationType this_type = cb_type_object(class_name);
        builder->frame->locals[local_index] = this_type;
        builder->initial_frame->locals[local_index] = this_type;
        ++local_index;
    }

    /* Add parameters to locals */
    for (ParameterList *p = params; p && !p->is_ellipsis; p = p->next)
    {
        CB_VerificationType param_type = cb_type_from_c_type(p->type);
        builder->frame->locals[local_index] = param_type;
        builder->initial_frame->locals[local_index] = param_type;
        ++local_index;

        /* Long and double take two slots */
        if (cb_type_slots(&param_type) == 2)
        {
            builder->frame->locals[local_index] = cb_type_top();
            builder->initial_frame->locals[local_index] = cb_type_top();
            ++local_index;
        }
    }

    builder->frame->locals_count = local_index;
    builder->initial_frame->locals_count = local_index;
    builder->max_locals = local_index;

    return builder;
}

void codebuilder_destroy(CodeBuilder *builder)
{
    if (!builder)
    {
        return;
    }

    /* Free owned frames */
    if (builder->frame)
    {
        free(builder->frame);
    }
    if (builder->initial_frame)
    {
        free(builder->initial_frame);
    }

    if (builder->branch_targets)
    {
        /* Free frames owned by branch targets (avoiding double-free) */
        for (int i = 0; i < builder->branch_target_count; ++i)
        {
            if (builder->branch_targets[i].frame)
            {
                /* Check if this frame was already freed (duplicate entry) */
                bool already_freed = false;
                for (int j = 0; j < i; ++j)
                {
                    if (builder->branch_targets[j].frame == builder->branch_targets[i].frame)
                    {
                        already_freed = true;
                        break;
                    }
                }
                if (!already_freed)
                {
                    free(builder->branch_targets[i].frame);
                }
            }
        }
        free(builder->branch_targets);
    }

    if (builder->labels)
    {
        /* Free individually allocated labels and their frames */
        for (int i = 0; i < builder->label_count; ++i)
        {
            if (builder->labels[i])
            {
                if (builder->labels[i]->frame)
                {
                    free(builder->labels[i]->frame);
                }
                free(builder->labels[i]);
            }
        }
        free(builder->labels);
    }

    if (builder->pending_jumps)
    {
        free(builder->pending_jumps);
    }

    /* Free switch context cases */
    for (int i = 0; i < builder->control_depth; ++i)
    {
        CB_ControlEntry *entry = &builder->control_stack[i];
        if (entry->kind == CB_CONTROL_SWITCH && entry->u.switch_ctx.cases)
        {
            free(entry->u.switch_ctx.cases);
        }
    }

    if (builder->control_stack)
    {
        free(builder->control_stack);
    }
}

/* ============================================================
 * Reachability Tracking API (javac-style alive flag)
 * ============================================================ */

bool codebuilder_is_alive(CodeBuilder *builder)
{
    return builder && builder->alive;
}

void codebuilder_mark_dead(CodeBuilder *builder)
{
    if (builder)
    {
        builder->alive = false;
    }
}

void codebuilder_mark_alive(CodeBuilder *builder)
{
    if (builder)
    {
        builder->alive = true;
    }
}
