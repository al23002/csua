/*
 * CodeBuilder StackMapTable Generation
 *
 * Generates CF_StackMapFrame array from CodeBuilder's recorded branch targets.
 * Handles differential encoding (same_frame, append_frame, etc.) separately
 * from the CodeBuilder's frame tracking logic.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codebuilder_stackmap.h"
#include "codebuilder_types.h"

/* ============================================================
 * Helper Functions
 * ============================================================ */

/* Insertion sort for branch targets by PC */
static void sort_branch_targets(CB_BranchTarget *targets, int count)
{
    for (int i = 1; i < count; i++)
    {
        CB_BranchTarget tmp = targets[i];
        int j = i;
        while (j > 0 && targets[j - 1].pc > tmp.pc)
        {
            targets[j] = targets[j - 1];
            j--;
        }
        targets[j] = tmp;
    }
}

/* Convert CB_VerificationType to CF_VerificationTypeInfo */
static CF_VerificationTypeInfo convert_type(CB_VerificationType *cb_type,
                                            CF_ConstantPool *cp)
{
    CF_VerificationTypeInfo cf_type = {};

    cf_type.tag = cb_type->tag;

    if (cb_type->tag == CF_VERIFICATION_OBJECT && cb_type->u.class_name)
    {
        const char *desc = cb_type->u.class_name;

        if (cp)
        {
            /* Handle different descriptor formats */
            if (desc[0] == 'L')
            {
                /* Object descriptor: Lcom/example/Class; */
                int len = strlen(desc);
                if (len > 2 && desc[len - 1] == ';')
                {
                    /* Extract class name without L and ; */
                    char *name = (char *)calloc(len - 1, sizeof(char));
                    strncpy(name, desc + 1, len - 2);
                    name[len - 2] = '\0';
                    cf_type.u.cpool_index = (uint16_t)cf_cp_add_class(cp, name);
                }
                else
                {
                    cf_type.u.cpool_index = (uint16_t)cf_cp_add_class(cp, desc);
                }
            }
            else if (desc[0] == '[')
            {
                /* Array descriptor: [I or [[Ljava/lang/String; */
                cf_type.u.cpool_index = (uint16_t)cf_cp_add_class(cp, desc);
            }
            else
            {
                /* Plain class name */
                cf_type.u.cpool_index = (uint16_t)cf_cp_add_class(cp, desc);
            }
        }
        else
        {
            cf_type.u.cpool_index = 0;
        }
    }
    else if (cb_type->tag == CF_VERIFICATION_UNINITIALIZED)
    {
        cf_type.u.offset = (uint16_t)cb_type->u.offset;
    }

    return cf_type;
}

/* Count the number of verification_type_info entries for locals.
 * In JVM StackMapTable, long/double are represented as a single entry
 * (the second slot TOP is implicit, not listed).
 * Standalone TOP values (from type merging) ARE listed. */
static int count_stackmap_locals(CB_Frame *frame)
{
    int entry_count = 0;
    int slot_count = 0;

    /* First find how many slots are effectively used */
    for (int i = 0; i < frame->locals_count; ++i)
    {
        CB_VerificationType *slot = &frame->locals[i];
        bool is_payload = slot->tag != CF_VERIFICATION_TOP;
        bool is_second_slot = (slot->tag == CF_VERIFICATION_TOP && i > 0 &&
                               cb_type_slots(&frame->locals[i - 1]) == 2);
        if (is_payload || is_second_slot)
        {
            slot_count = i + 1;
        }
    }

    /* Count entries: include TOPs except implicit ones after long/double */
    for (int i = 0; i < slot_count; ++i)
    {
        CB_VerificationType *slot = &frame->locals[i];
        /* Skip implicit TOP after long/double */
        bool is_implicit_top = (slot->tag == CF_VERIFICATION_TOP && i > 0 &&
                                cb_type_slots(&frame->locals[i - 1]) == 2);
        if (!is_implicit_top)
        {
            entry_count++;
        }
    }

    return entry_count;
}

/* Count the number of verification_type_info entries for stack.
 * Same logic as locals: long/double are 1 entry (implicit TOP not counted). */
static int count_stackmap_stack(CB_Frame *frame)
{
    int entry_count = 0;
    for (int i = 0; i < frame->stack_count; ++i)
    {
        CB_VerificationType *slot = &frame->stack[i];
        /* Skip implicit TOP after long/double */
        bool is_implicit_top = (slot->tag == CF_VERIFICATION_TOP && i > 0 &&
                                cb_type_slots(&frame->stack[i - 1]) == 2);
        if (!is_implicit_top)
        {
            entry_count++;
        }
    }
    return entry_count;
}

/* Get the slot count (including TOPs) for comparison purposes */
static int count_effective_slots(CB_Frame *frame)
{
    int count = 0;
    for (int i = 0; i < frame->locals_count; ++i)
    {
        CB_VerificationType *slot = &frame->locals[i];
        bool is_payload = slot->tag != CF_VERIFICATION_TOP;
        bool is_second_slot = (slot->tag == CF_VERIFICATION_TOP && i > 0 &&
                               cb_type_slots(&frame->locals[i - 1]) == 2);
        if (is_payload || is_second_slot)
        {
            count = i + 1;
        }
    }
    return count;
}

/* Check if two frames have the same locals */
static bool frames_locals_equal(CB_Frame *a, CB_Frame *b, int count)
{
    for (int i = 0; i < count; ++i)
    {
        if (!cb_type_equals(&a->locals[i], &b->locals[i]))
        {
            return false;
        }
    }
    return true;
}

/* Generate a single frame comparing to previous state */
static CF_StackMapFrame generate_frame(CB_Frame *initial,
                                       CB_Frame *prev,
                                       CB_Frame *curr,
                                       int prev_pc,
                                       int curr_pc,
                                       CF_ConstantPool *cp)
{
    CF_StackMapFrame frame = {};

    /* Calculate offset_delta */
    int offset_delta;
    if (prev == NULL)
    {
        /* First frame: offset_delta is the bytecode offset */
        offset_delta = curr_pc;
    }
    else
    {
        /* Subsequent frames: offset_delta is (curr_pc - prev_pc - 1) */
        offset_delta = curr_pc - prev_pc - 1;
    }

    frame.offset_delta = (uint16_t)offset_delta;

    /* Use ENTRY counts for chop/append frame decisions
     * JVM interprets chop_frame/append_frame K value as number of entries,
     * not slots. Long/Double are 1 entry but 2 slots. */
    int prev_entries = prev ? count_stackmap_locals(prev) : count_stackmap_locals(initial);
    int curr_entries = count_stackmap_locals(curr);
    /* Also track slots for type comparison */
    int prev_slots = prev ? count_effective_slots(prev) : count_effective_slots(initial);
    int curr_slots = count_effective_slots(curr);
    /* Stack: use entry count (Long/Double = 1 entry but 2 slots) */
    int stack_entries = count_stackmap_stack(curr);
    int stack_slots = curr->stack_count;

    /* Determine frame type based on changes */

    /* same_frame: no stack, same locals (compare by entries AND slots for correctness) */
    if (stack_entries == 0 && curr_entries == prev_entries && curr_slots == prev_slots)
    {
        CB_Frame *compare_frame = prev ? prev : initial;
        bool locals_same = frames_locals_equal(compare_frame, curr, prev_slots);

        if (locals_same)
        {
            if (offset_delta <= 63)
            {
                frame.frame_type = (uint8_t)offset_delta; /* same_frame */
            }
            else
            {
                frame.frame_type = 251; /* same_frame_extended */
            }
            return frame;
        }
    }

    /* same_locals_1_stack_item_frame: 1 stack entry, same locals */
    if (stack_entries == 1 && curr_entries == prev_entries && curr_slots == prev_slots)
    {
        CB_Frame *compare_frame = prev ? prev : initial;
        bool locals_same = frames_locals_equal(compare_frame, curr, prev_slots);

        if (locals_same)
        {
            frame.stack_count = 1;
            frame.stack = (CF_VerificationTypeInfo *)calloc(1, sizeof(CF_VerificationTypeInfo));
            frame.stack[0] = convert_type(&curr->stack[0], cp);

            if (offset_delta <= 63)
            {
                frame.frame_type = (uint8_t)(64 + offset_delta); /* same_locals_1_stack_item */
            }
            else
            {
                frame.frame_type = 247; /* same_locals_1_stack_item_extended */
            }
            return frame;
        }
    }

    /* chop_frame: no stack, fewer locals (by entries), and remaining locals unchanged
     * K = number of entries removed (1-3). JVM interprets K as entry count, not slots. */
    if (stack_entries == 0 && curr_entries < prev_entries && prev_entries - curr_entries <= 3)
    {
        CB_Frame *compare_frame = prev ? prev : initial;
        /* Verify remaining locals are the same type (compare by slots up to curr_slots) */
        if (frames_locals_equal(compare_frame, curr, curr_slots))
        {
            int k = prev_entries - curr_entries;
            frame.frame_type = (uint8_t)(251 - k); /* chop_frame: 248-250 */
            return frame;
        }
    }

    /* append_frame: no stack, more locals (1-3 entries), and existing locals unchanged
     * K = number of entries added (1-3). JVM interprets K as entry count. */
    if (stack_entries == 0 && curr_entries > prev_entries && curr_entries - prev_entries <= 3)
    {
        CB_Frame *compare_frame = prev ? prev : initial;
        /* Verify existing locals are the same type */
        if (!frames_locals_equal(compare_frame, curr, prev_slots))
        {
            /* Types changed - fall through to full_frame */
            goto full_frame;
        }
        int k = curr_entries - prev_entries;
        frame.frame_type = (uint8_t)(251 + k); /* append_frame: 252-254 */
        /* Count actual entries in the appended portion (skip TOPs) */
        int append_entries = 0;
        for (int i = prev_slots; i < curr_slots; ++i)
        {
            if (curr->locals[i].tag != CF_VERIFICATION_TOP)
            {
                append_entries++;
            }
        }
        frame.locals_count = (uint16_t)append_entries;
        frame.locals = (CF_VerificationTypeInfo *)calloc(append_entries, sizeof(CF_VerificationTypeInfo));
        int j = 0;
        for (int i = prev_slots; i < curr_slots; ++i)
        {
            if (curr->locals[i].tag != CF_VERIFICATION_TOP)
            {
                frame.locals[j++] = convert_type(&curr->locals[i], cp);
            }
        }
        return frame;
    }

full_frame:
    /* full_frame: any other case */
    frame.frame_type = 255;
    frame.locals_count = (uint16_t)curr_entries;
    frame.stack_count = (uint16_t)stack_entries;

    if (curr_entries > 0)
    {
        frame.locals = (CF_VerificationTypeInfo *)calloc(
            curr_entries, sizeof(CF_VerificationTypeInfo));
        int j = 0;
        for (int i = 0; i < curr_slots; ++i)
        {
            /* Skip implicit TOP after long/double */
            bool is_implicit_top = (curr->locals[i].tag == CF_VERIFICATION_TOP && i > 0 &&
                                    cb_type_slots(&curr->locals[i - 1]) == 2);
            if (!is_implicit_top)
            {
                frame.locals[j++] = convert_type(&curr->locals[i], cp);
            }
        }
    }

    if (stack_entries > 0)
    {
        frame.stack = (CF_VerificationTypeInfo *)calloc(
            stack_entries, sizeof(CF_VerificationTypeInfo));
        int j = 0;
        for (int i = 0; i < stack_slots; ++i)
        {
            /* Skip implicit TOP after long/double */
            bool is_implicit_top = (curr->stack[i].tag == CF_VERIFICATION_TOP && i > 0 &&
                                    cb_type_slots(&curr->stack[i - 1]) == 2);
            if (!is_implicit_top)
            {
                frame.stack[j++] = convert_type(&curr->stack[i], cp);
            }
        }
    }

    return frame;
}

/* ============================================================
 * Public API
 * ============================================================ */

CF_StackMapFrame *codebuilder_generate_stackmap(CodeBuilder *builder,
                                                CF_ConstantPool *cp,
                                                int *frame_count)
{
    if (!builder || !frame_count)
    {
        if (frame_count)
            *frame_count = 0;
        return NULL;
    }

    int code_size = method_code_size(builder->method);

    if (builder->branch_target_count == 0)
    {
        *frame_count = 0;
        return NULL;
    }

    /* Sort branch targets by PC */
    sort_branch_targets(builder->branch_targets, builder->branch_target_count);

    /* Remove duplicates (keep PC=0 if there's a branch target there) */
    int write_idx = 0;
    int last_pc = -1;
    for (int i = 0; i < builder->branch_target_count; ++i)
    {
        CB_BranchTarget *target = &builder->branch_targets[i];
        if (target->pc == last_pc)
        {
            /* Free skipped entry's frame to prevent leak */
            if (target->frame)
            {
                free(target->frame);
                target->frame = NULL;
            }
            continue;
        }
        if (write_idx != i)
        {
            builder->branch_targets[write_idx] = *target;
            /* Clear original to prevent double-free */
            target->frame = NULL;
        }
        last_pc = target->pc;
        write_idx++;
    }

    int unique_count = write_idx;
    builder->branch_target_count = unique_count;
    if (unique_count == 0)
    {
        *frame_count = 0;
        return NULL;
    }

    /* Allocate result array */
    CF_StackMapFrame *frames = (CF_StackMapFrame *)calloc(
        unique_count, sizeof(CF_StackMapFrame));

    CB_Frame *prev_frame = NULL;
    int prev_pc = 0;
    int output_count = 0;

    for (int i = 0; i < unique_count; ++i)
    {
        CB_BranchTarget *target = &builder->branch_targets[i];

        /* Skip frames at or beyond code end - no instructions there to verify */
        if (target->pc >= code_size)
        {
            continue;
        }

        frames[output_count] = generate_frame(
            builder->initial_frame,
            prev_frame,
            target->frame,
            prev_pc,
            target->pc,
            cp);

        prev_frame = target->frame;
        prev_pc = target->pc;
        output_count++;
    }

    *frame_count = output_count;
    return frames;
}

void codebuilder_free_stackmap(CF_StackMapFrame *frames, int count)
{
    if (!frames)
    {
        return;
    }

    for (int i = 0; i < count; ++i)
    {
        if (frames[i].locals)
        {
            free(frames[i].locals);
        }
        if (frames[i].stack)
        {
            free(frames[i].stack);
        }
    }
    free(frames);
}
