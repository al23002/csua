/*
 * CodeBuilder Label - Label and Jump Management
 *
 * Handles:
 * - Label creation and placement
 * - Jump instruction generation
 * - Branch target recording
 * - Pending jump resolution
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codebuilder_label.h"
#include "codebuilder_core.h"
#include "codebuilder_internal.h"
#include "codebuilder_part3.h"
#include "codebuilder_types.h"
#include "classfile_opcode.h"

/* ============================================================
 * Branch Target Recording
 * ============================================================ */

static void ensure_branch_target_capacity(CodeBuilder *builder)
{
    if (builder->branch_target_count >= builder->branch_target_capacity)
    {
        int new_capacity = builder->branch_target_capacity == 0
                               ? 16
                               : builder->branch_target_capacity * 2;
        CB_BranchTarget *new_targets = (CB_BranchTarget *)calloc(
            new_capacity, sizeof(CB_BranchTarget));
        for (int i = 0; i < builder->branch_target_count; i++)
        {
            new_targets[i] = builder->branch_targets[i];
        }
        builder->branch_targets = new_targets;
        builder->branch_target_capacity = new_capacity;
    }
}

void codebuilder_record_branch_target(CodeBuilder *builder, int target_pc)
{
    if (!builder)
    {
        return;
    }

    /* Check if already recorded */
    for (int i = 0; i < builder->branch_target_count; ++i)
    {
        if (builder->branch_targets[i].pc == target_pc)
        {
            cb_merge_frame(builder->branch_targets[i].frame, builder->frame);
            return;
        }
    }

    ensure_branch_target_capacity(builder);

    CB_BranchTarget *target = &builder->branch_targets[builder->branch_target_count++];
    target->pc = target_pc;
    target->frame = cb_create_frame();
    cb_copy_frame(target->frame, builder->frame);
    target->is_exception = false;
}

/*
 * Record branch target with a specific frame (used for already-placed labels)
 */
void codebuilder_record_branch_target_with_frame(CodeBuilder *builder,
                                                 int target_pc,
                                                 CB_Frame *frame)
{
    if (!builder)
    {
        return;
    }

    /* Check if already recorded */
    for (int i = 0; i < builder->branch_target_count; ++i)
    {
        if (builder->branch_targets[i].pc == target_pc)
        {
            cb_merge_frame(builder->branch_targets[i].frame, frame);
            return;
        }
    }

    ensure_branch_target_capacity(builder);

    CB_BranchTarget *target = &builder->branch_targets[builder->branch_target_count++];
    target->pc = target_pc;
    target->frame = cb_create_frame();
    cb_copy_frame(target->frame, frame);
    target->is_exception = false;
}

void codebuilder_record_exception_handler(CodeBuilder *builder, int handler_pc, const char *exception_class)
{
    if (!builder)
    {
        return;
    }

    ensure_branch_target_capacity(builder);

    CB_BranchTarget *target = &builder->branch_targets[builder->branch_target_count++];
    target->pc = handler_pc;
    target->frame = cb_create_frame();

    /* Exception handlers start with the initial locals and exception on stack */
    cb_copy_frame(target->frame, builder->initial_frame);
    target->frame->stack_count = 1;
    target->frame->stack[0] = cb_type_object(exception_class ? exception_class
                                                             : "Ljava/lang/Throwable;");
    target->is_exception = true;
}

/* ============================================================
 * Label API Implementation
 * ============================================================ */

void cb_ensure_label_capacity(CodeBuilder *builder)
{
    if (builder->label_count < builder->label_capacity)
    {
        return;
    }

    int new_capacity = builder->label_capacity == 0
                           ? 16
                           : builder->label_capacity * 2;
    CB_Label **new_labels = (CB_Label **)calloc(new_capacity, sizeof(struct CB_Label_tag *));
    for (int i = 0; i < builder->label_count; i++)
    {
        new_labels[i] = builder->labels[i];
    }
    builder->labels = new_labels;
    builder->label_capacity = new_capacity;
}

void cb_ensure_pending_jump_capacity(CodeBuilder *builder)
{
    if (builder->pending_jump_count < builder->pending_jump_capacity)
    {
        return;
    }

    int new_capacity = builder->pending_jump_capacity == 0
                           ? 16
                           : builder->pending_jump_capacity * 2;
    CB_PendingJump *new_jumps = (CB_PendingJump *)calloc(new_capacity, sizeof(struct CB_PendingJump_tag));
    for (int i = 0; i < builder->pending_jump_count; i++)
    {
        new_jumps[i] = builder->pending_jumps[i];
    }
    builder->pending_jumps = new_jumps;
    builder->pending_jump_capacity = new_capacity;
}

int codebuilder_current_pc(CodeBuilder *builder)
{
    if (!builder || !builder->method)
    {
        return 0;
    }
    return method_code_size(builder->method);
}

CB_Label *codebuilder_create_label(CodeBuilder *builder)
{
    if (!builder)
    {
        return NULL;
    }

    cb_ensure_label_capacity(builder);

    /* Allocate label individually to prevent pointer invalidation on realloc */
    CB_Label *label = (CB_Label *)calloc(1, sizeof(CB_Label));
    label->id = builder->label_count;
    label->pc = 0xFFFFFFFF; /* Unresolved */
    label->is_placed = false;
    label->is_loop_header = false;
    label->frame_recorded = false;
    label->frame_saved = false;
    label->jump_only = false;
    label->is_jump_target = false;

    /* Diagnostic fields */
    label->name = NULL;
    label->jump_sources = NULL;
    label->jump_source_count = 0;
    label->jump_source_capacity = 0;
    label->frame = cb_create_frame();

    builder->labels[builder->label_count++] = label;
    return label;
}

void codebuilder_place_label(CodeBuilder *builder, CB_Label *label)
{
    if (!builder || !label)
    {
        return;
    }

    /* Record current PC as label position */
    label->pc = codebuilder_current_pc(builder);
    label->is_placed = true;

    /*
     * Frame state handling (javac-style alive flag):
     * - If current code is unreachable (after goto/return/throw) and we have
     *   a saved frame from a jump, restore it
     * - If current code is reachable (fallthrough), use current frame state
     * - If unreachable and no saved frame, label is dead code
     */
    if (!builder->alive && label->frame_saved)
    {
        /* Dead code path but reachable via saved frame - restore frame and mark alive */
        codebuilder_restore_frame_safe(builder, label->frame);
        codebuilder_mark_alive(builder);
        /* Record branch target for StackMapTable since this label follows unreachable code */
        if (!label->frame_recorded)
        {
            codebuilder_record_branch_target_with_frame(builder, label->pc, label->frame);
            label->frame_recorded = true;
        }
    }
    else if (builder->alive)
    {
        /* Live code path (fallthrough) - capture current frame state, stays alive */
        if (label->frame_saved)
        {
            cb_merge_frame(label->frame, builder->frame);
            /* Update branch target frame if already recorded */
            if (label->frame_recorded && (label->is_jump_target || label->is_loop_header))
            {
                for (int i = 0; i < builder->branch_target_count; ++i)
                {
                    if (builder->branch_targets[i].pc == label->pc)
                    {
                        cb_merge_frame(builder->branch_targets[i].frame, builder->frame);
                        break;
                    }
                }
            }
        }
        else
        {
            cb_copy_frame(label->frame, builder->frame);
            label->frame_saved = true; /* Mark frame as valid for later backward jumps */
        }
    }
    else
    {
        /*
         * !alive && !frame_saved -> unreachable label following dead code.
         * In C, goto can reach any named label, so we must generate code.
         *
         * The current builder->frame still contains the frame state from before
         * the dead code (codebuilder_mark_dead doesn't clear the frame).
         * We use this frame as-is, which preserves local variable types.
         *
         * Save the current frame to the label for potential backward jumps.
         * Also record a StackMapFrame here because JVM requires one at any
         * branch target, and C labels can be targets of later goto statements.
         */
        cb_copy_frame(label->frame, builder->frame);
        label->frame_saved = true;
        codebuilder_record_branch_target_with_frame(builder, label->pc, label->frame);
        label->frame_recorded = true;
        codebuilder_mark_alive(builder);
    }

    /* Record branch target for StackMapTable if needed */
    if (!label->frame_recorded)
    {
        /*
         * Only record if this is a forward jump target.
         * Loop headers are recorded when backward jump occurs (in cb_save_frame_to_label),
         * not at placement time, to avoid generating StackMapFrame for loops that
         * exit via break/goto without any continue/loop-back.
         */
        if (label->is_jump_target)
        {
            codebuilder_record_branch_target_with_frame(builder, label->pc, label->frame);
            label->frame_recorded = true;
        }
    }
}

void codebuilder_mark_loop_header(CodeBuilder *builder, CB_Label *label)
{
    if (!builder || !label)
    {
        return;
    }

    label->is_loop_header = true;
}

void codebuilder_mark_jump_only(CodeBuilder *builder, CB_Label *label)
{
    if (!builder || !label)
    {
        return;
    }

    label->jump_only = true;
}

void cb_add_pending_jump(CodeBuilder *builder, int jump_pc, CB_Label *target)
{
    cb_ensure_pending_jump_capacity(builder);

    CB_PendingJump *pending = &builder->pending_jumps[builder->pending_jump_count++];
    pending->jump_pc = jump_pc;
    pending->target = target;
}

/*
 * Record jump source for diagnostic purposes
 */
static void cb_record_jump_source(CodeBuilder *builder, CB_Label *target, const char *context)
{
    /* Ensure capacity */
    if (target->jump_source_count >= target->jump_source_capacity)
    {
        int new_capacity = target->jump_source_capacity == 0
                               ? 4
                               : target->jump_source_capacity * 2;
        CB_JumpSource *new_sources = (CB_JumpSource *)calloc(new_capacity, sizeof(CB_JumpSource));
        for (int i = 0; i < target->jump_source_count; i++)
        {
            new_sources[i] = target->jump_sources[i];
        }
        target->jump_sources = new_sources;
        target->jump_source_capacity = new_capacity;
    }

    CB_JumpSource *src = &target->jump_sources[target->jump_source_count++];
    src->pc = codebuilder_current_pc(builder);
    src->line = 0; /* TODO: track source line */
    src->frame = cb_create_frame();
    cb_copy_frame(src->frame, builder->frame);
    src->context = context;
}

/*
 * Save current frame to target label for later restoration.
 * Called when jumping to an unplaced label.
 * When the label is placed, this frame will be restored automatically.
 */
static void cb_save_frame_to_label_with_context(CodeBuilder *builder, CB_Label *target, const char *context)
{
    /* Record jump source for diagnostics */
    cb_record_jump_source(builder, target, context);

    /* Mark as jump target for StackMapTable generation */
    target->is_jump_target = true;

    if (target->is_placed)
    {
        /*
         * Backward jump: label already placed.
         * First merge the label's saved frame (from placement time) with current frame
         * to find the minimum common frame state. Then record this merged frame
         * as the branch target.
         *
         * This ensures the StackMapFrame has the minimum locals_count across all
         * paths to this label (needed for JVM frame assignability).
         */
        if (target->frame_saved)
        {
            /* Merge to find minimum common frame state */
            cb_merge_frame(target->frame, builder->frame);
            /* Record the merged (minimum) frame as branch target */
            codebuilder_record_branch_target_with_frame(builder, target->pc, target->frame);
        }
        else
        {
            /* No saved frame - use current frame */
            codebuilder_record_branch_target_with_frame(builder, target->pc, builder->frame);
        }
    }
    else
    {
        /* Forward jump: label not yet placed */
        if (!target->frame_saved)
        {
            cb_copy_frame(target->frame, builder->frame);
            target->frame_saved = true;
        }
        else
        {
            cb_merge_frame(target->frame, builder->frame);
            /* If branch target was already recorded (from place_label),
             * update it with the merged frame to ensure consistency */
            if (target->frame_recorded)
            {
                codebuilder_record_branch_target_with_frame(builder, target->pc, target->frame);
            }
        }
    }
}

/* Use builder's jump_context if set, otherwise "unknown" */
static void cb_save_frame_to_label(CodeBuilder *builder, CB_Label *target)
{
    const char *ctx = builder->jump_context ? builder->jump_context : "unknown";
    cb_save_frame_to_label_with_context(builder, target, ctx);
    builder->jump_context = NULL; /* auto-clear after use */
}

void codebuilder_set_jump_context(CodeBuilder *builder, const char *context)
{
    if (builder)
    {
        builder->jump_context = context;
    }
}

void cb_write_s2_at_pc(CodeBuilder *builder, int pc, int value)
{
    MethodCode *mc = builder->method;
    method_code_write_u2_at(mc, pc, (uint16_t)value);
}

void codebuilder_jump(CodeBuilder *builder, CB_Label *target)
{
    if (!builder || !target)
    {
        return;
    }

    /* Skip emitting dead code - previous instruction was unconditional jump/return */
    if (!builder->alive)
    {
        return;
    }

    /* Save frame BEFORE goto (goto clears stack tracking for dead code) */
    cb_save_frame_to_label(builder, target);

    int jump_pc = codebuilder_current_pc(builder);

    if (target->is_placed)
    {
        int32_t offset = (int32_t)target->pc - (int32_t)jump_pc;
        codebuilder_build_goto(builder, offset);
    }
    else
    {
        codebuilder_build_goto(builder, 0);
        cb_add_pending_jump(builder, jump_pc, target);
    }

    /* Unconditional jump - code after this is unreachable */
    codebuilder_mark_dead(builder);
}

void codebuilder_jump_if(CodeBuilder *builder, CB_Label *target)
{
    if (!builder || !target)
    {
        return;
    }

    int jump_pc = codebuilder_current_pc(builder);

    if (target->is_placed)
    {
        int32_t offset = (int32_t)target->pc - (int32_t)jump_pc;
        codebuilder_build_if(builder, IF_NE, offset);
    }
    else
    {
        codebuilder_build_if(builder, IF_NE, 0);
        cb_add_pending_jump(builder, jump_pc, target);
    }

    /* Save frame AFTER jump instruction (ifne pops one value from stack) */
    cb_save_frame_to_label(builder, target);
}

void codebuilder_jump_if_op(CodeBuilder *builder, IfCond cond, CB_Label *target)
{
    if (!builder || !target)
    {
        return;
    }

    int jump_pc = codebuilder_current_pc(builder);

    if (target->is_placed)
    {
        int32_t offset = (int32_t)target->pc - (int32_t)jump_pc;
        codebuilder_build_if(builder, cond, offset);
    }
    else
    {
        codebuilder_build_if(builder, cond, 0);
        cb_add_pending_jump(builder, jump_pc, target);
    }

    /* Save frame AFTER jump instruction (if* pops one value from stack) */
    cb_save_frame_to_label(builder, target);
}

void codebuilder_jump_if_not(CodeBuilder *builder, CB_Label *target)
{
    if (!builder || !target)
    {
        return;
    }

    int jump_pc = codebuilder_current_pc(builder);

    if (target->is_placed)
    {
        int32_t offset = (int32_t)target->pc - (int32_t)jump_pc;
        codebuilder_build_if(builder, IF_EQ, offset);
    }
    else
    {
        codebuilder_build_if(builder, IF_EQ, 0);
        cb_add_pending_jump(builder, jump_pc, target);
    }

    /* Save frame AFTER jump instruction (ifeq pops one value from stack) */
    cb_save_frame_to_label(builder, target);
}

void codebuilder_jump_if_icmp(CodeBuilder *builder, IntCmpCond cond, CB_Label *target)
{
    if (!builder || !target)
    {
        return;
    }

    int jump_pc = codebuilder_current_pc(builder);

    if (target->is_placed)
    {
        int32_t offset = (int32_t)target->pc - (int32_t)jump_pc;
        codebuilder_build_if_icmp(builder, cond, offset);
    }
    else
    {
        codebuilder_build_if_icmp(builder, cond, 0);
        cb_add_pending_jump(builder, jump_pc, target);
    }

    /* Save frame AFTER jump instruction (if_icmp* pops two values from stack) */
    cb_save_frame_to_label(builder, target);
}

void codebuilder_jump_if_acmp(CodeBuilder *builder, ACmpCond cond, CB_Label *target)
{
    if (!builder || !target)
    {
        return;
    }

    int jump_pc = codebuilder_current_pc(builder);

    if (target->is_placed)
    {
        int32_t offset = (int32_t)target->pc - (int32_t)jump_pc;
        codebuilder_build_if_acmp(builder, cond, offset);
    }
    else
    {
        codebuilder_build_if_acmp(builder, cond, 0);
        cb_add_pending_jump(builder, jump_pc, target);
    }

    /* Save frame AFTER jump instruction (if_acmp* pops two references from stack) */
    cb_save_frame_to_label(builder, target);
}

void codebuilder_jump_if_null(CodeBuilder *builder, CB_Label *target)
{
    if (!builder || !target)
    {
        return;
    }

    int jump_pc = codebuilder_current_pc(builder);

    if (target->is_placed)
    {
        int32_t offset = (int32_t)target->pc - (int32_t)jump_pc;
        codebuilder_build_ifnull(builder, offset);
    }
    else
    {
        codebuilder_build_ifnull(builder, 0);
        cb_add_pending_jump(builder, jump_pc, target);
    }

    /* Save frame AFTER jump instruction (ifnull pops one reference from stack) */
    cb_save_frame_to_label(builder, target);
}

void codebuilder_jump_if_not_null(CodeBuilder *builder, CB_Label *target)
{
    if (!builder || !target)
    {
        return;
    }

    int jump_pc = codebuilder_current_pc(builder);

    if (target->is_placed)
    {
        int32_t offset = (int32_t)target->pc - (int32_t)jump_pc;
        codebuilder_build_ifnonnull(builder, offset);
    }
    else
    {
        codebuilder_build_ifnonnull(builder, 0);
        cb_add_pending_jump(builder, jump_pc, target);
    }

    /* Save frame AFTER jump instruction (ifnonnull pops one reference from stack) */
    cb_save_frame_to_label(builder, target);
}

void codebuilder_resolve_jumps(CodeBuilder *builder)
{
    if (!builder)
    {
        return;
    }

    for (int i = 0; i < builder->pending_jump_count; ++i)
    {
        CB_PendingJump *pending = &builder->pending_jumps[i];
        CB_Label *target = pending->target;

        if (!target->is_placed)
        {
            fprintf(stderr, "unresolved jump target: label %u\n", target->id);
            exit(1);
        }

        int32_t offset = (int32_t)target->pc - (int32_t)pending->jump_pc;
        if (offset < -32768 || offset > 32767)
        {
            fprintf(stderr, "jump offset %d out of range\n", offset);
            exit(1);
        }

        /* Patch the jump offset (at jump_pc + 1) */
        cb_write_s2_at_pc(builder, pending->jump_pc + 1, (int16_t)offset);

        /* Record branch target for StackMapTable if not already recorded */
        if (!target->frame_recorded)
        {
            /* Use target->frame (saved during forward jump) instead of builder->frame
             * builder->frame at this point is the end-of-function state, which is wrong */
            codebuilder_record_branch_target_with_frame(builder, target->pc, target->frame);
            target->frame_recorded = true;
        }
    }

    /* Clear pending jumps */
    builder->pending_jump_count = 0;
}

/* ============================================================
 * Label Diagnostics
 * ============================================================ */

void codebuilder_set_label_name(CB_Label *label, const char *name)
{
    if (label)
    {
        label->name = name;
    }
}

void codebuilder_dump_label_info(CodeBuilder *builder, CB_Label *label)
{
    if (!label)
    {
        return;
    }

    const char *name = label->name ? label->name : "(unnamed)";
    fprintf(stderr, "\n=== Label %d [%s] ===\n", label->id, name);
    fprintf(stderr, "  pc: %d, placed: %s, jump_target: %s\n",
            label->pc, label->is_placed ? "yes" : "no",
            label->is_jump_target ? "yes" : "no");
    fprintf(stderr, "  frame_saved: %s, frame_recorded: %s\n",
            label->frame_saved ? "yes" : "no",
            label->frame_recorded ? "yes" : "no");

    if (label->frame)
    {
        fprintf(stderr, "  final frame: locals_count=%d, stack_count=%d\n",
                label->frame->locals_count, label->frame->stack_count);
    }

    fprintf(stderr, "  jump sources (%d):\n", label->jump_source_count);
    for (int i = 0; i < label->jump_source_count; i++)
    {
        CB_JumpSource *src = &label->jump_sources[i];
        fprintf(stderr, "    [%d] pc=%d context=%s locals_count=%d\n",
                i, src->pc, src->context ? src->context : "?",
                src->frame ? src->frame->locals_count : -1);
        /* Show locals[33] type for debugging yyparse issue */
        if (src->frame && src->frame->locals_count > 33)
        {
            fprintf(stderr, "        locals[33] = %s\n",
                    cb_type_name(&src->frame->locals[33]));
        }
    }

    /* Also show label->frame locals[33] */
    if (label->frame && label->frame->locals_count > 33)
    {
        fprintf(stderr, "  label->frame locals[33] = %s\n",
                cb_type_name(&label->frame->locals[33]));
    }
}

void codebuilder_dump_all_labels(CodeBuilder *builder)
{
    if (!builder)
    {
        return;
    }

    fprintf(stderr, "\n========== All Labels for %s ==========\n",
            builder->method_name ? builder->method_name : "<unknown>");

    for (int i = 0; i < builder->label_count; i++)
    {
        CB_Label *label = builder->labels[i];
        if (label && label->is_jump_target)
        {
            codebuilder_dump_label_info(builder, label);
        }
    }
    fprintf(stderr, "========================================\n\n");
}

/* Find and print labels with potential frame issues */
void codebuilder_diagnose_frame_issues(CodeBuilder *builder)
{
    if (!builder)
    {
        return;
    }

    bool found_issues = false;

    for (int i = 0; i < builder->label_count; i++)
    {
        CB_Label *label = builder->labels[i];
        if (!label || !label->is_jump_target || label->jump_source_count < 2)
        {
            continue;
        }

        /* Check if different sources have different locals_count */
        int min_locals = -1;
        int max_locals = -1;
        for (int j = 0; j < label->jump_source_count; j++)
        {
            CB_JumpSource *src = &label->jump_sources[j];
            if (src->frame)
            {
                int lc = src->frame->locals_count;
                if (min_locals < 0 || lc < min_locals)
                {
                    min_locals = lc;
                }
                if (max_locals < 0 || lc > max_locals)
                {
                    max_locals = lc;
                }
            }
        }

        if (min_locals >= 0 && max_locals >= 0 && min_locals != max_locals)
        {
            if (!found_issues)
            {
                fprintf(stderr, "\n=== Frame Issues Detected in %s ===\n",
                        builder->method_name ? builder->method_name : "<unknown>");
                found_issues = true;
            }

            const char *name = label->name ? label->name : "(unnamed)";
            fprintf(stderr, "\nLabel %d [%s] at pc=%d:\n", label->id, name, label->pc);
            fprintf(stderr, "  locals_count varies: min=%d max=%d\n", min_locals, max_locals);

            for (int j = 0; j < label->jump_source_count; j++)
            {
                CB_JumpSource *src = &label->jump_sources[j];
                fprintf(stderr, "  source[%d]: pc=%d ctx=%s locals=%d\n",
                        j, src->pc, src->context ? src->context : "?",
                        src->frame ? src->frame->locals_count : -1);

                /* Show locals that differ from min_locals */
                if (src->frame && src->frame->locals_count > min_locals)
                {
                    fprintf(stderr, "    extra locals [%d..%d]:\n",
                            min_locals, src->frame->locals_count - 1);
                    for (int k = min_locals; k < src->frame->locals_count && k < min_locals + 5; k++)
                    {
                        CB_VerificationType *t = &src->frame->locals[k];
                        fprintf(stderr, "      [%d] = %s\n", k, cb_type_name(t));
                    }
                    if (src->frame->locals_count > min_locals + 5)
                    {
                        fprintf(stderr, "      ... and %d more\n",
                                src->frame->locals_count - min_locals - 5);
                    }
                }
            }
        }
    }

    if (found_issues)
    {
        fprintf(stderr, "=====================================\n\n");
    }
}
