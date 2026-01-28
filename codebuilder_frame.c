/*
 * CodeBuilder Frame - Frame State, Stack, and Locals Management
 *
 * Handles:
 * - Frame state creation and manipulation
 * - Stack operations (push/pop with type tracking)
 * - Local variable allocation and management
 * - Block scope tracking
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codebuilder_frame.h"
#include "codebuilder_internal.h"
#include "codebuilder_label.h"
#include "codebuilder_types.h"

/* ============================================================
 * Frame State Helpers
 * ============================================================ */

CB_Frame *cb_create_frame()
{
    CB_Frame *frame = (CB_Frame *)calloc(1, sizeof(CB_Frame));
    if (!frame)
    {
        fprintf(stderr, "cb_create_frame: calloc failed\n");
        exit(1);
    }

    /* Allocate locals and stack arrays */
    frame->locals = (CB_VerificationType *)calloc(CB_MAX_LOCALS, sizeof(CB_VerificationType));
    frame->stack = (CB_VerificationType *)calloc(CB_MAX_STACK, sizeof(CB_VerificationType));

    frame->locals_count = 0;
    frame->stack_count = 0;

    /* Initialize all slots to TOP (calloc already sets to 0 = CF_VERIFICATION_TOP) */
    for (int i = 0; i < CB_MAX_LOCALS; ++i)
    {
        frame->locals[i].tag = CF_VERIFICATION_TOP;
    }
    for (int i = 0; i < CB_MAX_STACK; ++i)
    {
        frame->stack[i].tag = CF_VERIFICATION_TOP;
    }
    return frame;
}

void cb_copy_frame(CB_Frame *dest, const CB_Frame *src)
{
    if (!dest || !src)
    {
        fprintf(stderr, "cb_copy_frame: NULL pointer (dest=%p, src=%p)\n", (void *)dest, (void *)src);
        exit(1);
    }

    /* Deep copy: copy counts */
    dest->locals_count = src->locals_count;
    dest->stack_count = src->stack_count;

    /* Deep copy: copy array contents (not pointers) */
    for (int i = 0; i < CB_MAX_LOCALS; ++i)
    {
        dest->locals[i] = src->locals[i];
    }
    for (int i = 0; i < CB_MAX_STACK; ++i)
    {
        dest->stack[i] = src->stack[i];
    }
}

static int cb_effective_locals_count(CB_Frame *frame)
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

/* Helper: Check if a descriptor represents an array type */
static bool is_array_descriptor(const char *desc)
{
    return desc && desc[0] == '[';
}

/* Helper: Get array dimension count */
static int array_dimension(const char *desc)
{
    if (!desc)
    {
        return 0;
    }
    int dim = 0;
    while (desc[dim] == '[')
    {
        dim++;
    }
    return dim;
}

/* Helper: Check if descriptors have compatible array element types */
static bool array_elements_compatible(const char *a_desc, const char *b_desc)
{
    if (!a_desc || !b_desc)
    {
        return false;
    }

    /* Find element type (skip all '[') */
    const char *a_elem = a_desc;
    const char *b_elem = b_desc;
    while (*a_elem == '[')
    {
        a_elem++;
    }
    while (*b_elem == '[')
    {
        b_elem++;
    }

    /* If both are primitive arrays, they must have same element type */
    if (*a_elem != 'L' && *b_elem != 'L')
    {
        return *a_elem == *b_elem;
    }

    /* If one is primitive and other is object, not compatible */
    if ((*a_elem != 'L') != (*b_elem != 'L'))
    {
        return false;
    }

    /* Both are object arrays - compatible (merge to Object[]) */
    return true;
}

/* Merge two verification types using JVM type system rules */
static CB_VerificationType cb_merge_type(CB_VerificationType a, CB_VerificationType b)
{
    /* Same type - no merge needed */
    if (cb_type_equals(&a, &b))
    {
        return a;
    }

    /* null is assignable to any reference type */
    if (a.tag == CF_VERIFICATION_NULL && b.tag == CF_VERIFICATION_OBJECT)
    {
        return b;
    }
    if (b.tag == CF_VERIFICATION_NULL && a.tag == CF_VERIFICATION_OBJECT)
    {
        return a;
    }

    /* Both are null - return null */
    if (a.tag == CF_VERIFICATION_NULL && b.tag == CF_VERIFICATION_NULL)
    {
        return a;
    }

    /* Both are objects - need to find common supertype */
    if (a.tag == CF_VERIFICATION_OBJECT && b.tag == CF_VERIFICATION_OBJECT)
    {
        const char *a_desc = a.u.class_name;
        const char *b_desc = b.u.class_name;

        /* Handle null/missing descriptors */
        if (!a_desc || !b_desc)
        {
            return cb_type_object("Ljava/lang/Object;");
        }

        /* Same class - already handled by cb_type_equals above */

        /* Array type handling */
        bool a_is_array = is_array_descriptor(a_desc);
        bool b_is_array = is_array_descriptor(b_desc);

        if (a_is_array && b_is_array)
        {
            int a_dim = array_dimension(a_desc);
            int b_dim = array_dimension(b_desc);

            if (a_dim == b_dim)
            {
                /* Same dimension - check element compatibility */
                if (array_elements_compatible(a_desc, b_desc))
                {
                    /* Find element type after all '[' */
                    const char *a_elem = a_desc + a_dim;
                    const char *b_elem = b_desc + b_dim;

                    /* If both are primitive, already checked by array_elements_compatible
                     * If both are objects, merge to Object[] at same dimension */
                    if (*a_elem == 'L' && *b_elem == 'L')
                    {
                        /* Build descriptor: [[[...Ljava/lang/Object; */
                        char *merged_desc = (char *)calloc(a_dim + 22, sizeof(char));
                        for (int i = 0; i < a_dim; i++)
                        {
                            merged_desc[i] = '[';
                        }
                        strcpy(merged_desc + a_dim, "Ljava/lang/Object;");
                        return cb_type_object(merged_desc);
                    }
                    else
                    {
                        /* Primitive arrays with different element types - fall through to Object */
                    }
                }
            }
            /* Different dimensions or incompatible elements - both are objects, merge to Object */
            return cb_type_object("Ljava/lang/Object;");
        }
        else if (a_is_array || b_is_array)
        {
            /* One array, one non-array object - merge to Object */
            return cb_type_object("Ljava/lang/Object;");
        }

        /* Both non-array objects - merge to common supertype
         * TODO: implement proper class hierarchy analysis
         * For now, merge to Object */
        return cb_type_object("Ljava/lang/Object;");
    }

    /* Different primitive types or incompatible types - use TOP */
    return cb_type_top();
}

/* Global flag to enable verbose merge diagnostics */
static bool cb_merge_verbose = false;

void cb_set_merge_verbose(bool verbose)
{
    cb_merge_verbose = verbose;
}

void cb_merge_frame(CB_Frame *dest, const CB_Frame *src)
{
    /*
     * JVM StackMapFrame rule: for a branch to be valid, the source frame
     * must be "assignable" to the target StackMapFrame. For locals, this
     * means the StackMapFrame's locals must match or be supertypes of the
     * source frame's locals.
     *
     * When multiple jumps target the same label from different scopes
     * (with different locals_count), we must use the MINIMUM locals_count.
     * This ensures all source frames can be "assignable" to the target.
     * Extra locals beyond min_locals are not visible at the merge point.
     */
    int min_locals = dest->locals_count;
    if (src->locals_count < min_locals)
    {
        min_locals = src->locals_count;
    }

#ifdef DEBUG_FRAME
    if (cb_merge_verbose && dest->locals_count != src->locals_count)
    {
        fprintf(stderr, "[merge] locals_count: dest=%d src=%d -> min=%d\n",
                dest->locals_count, src->locals_count, min_locals);
    }
    /* Track locals[33] changes for debugging */
    if (dest->locals_count > 33 && src->locals_count <= 33)
    {
        fprintf(stderr, "[merge] WARNING: dest has locals[33]=%s but src only has %d locals\n",
                cb_type_name(&dest->locals[33]), src->locals_count);
    }
#endif

    for (int i = 0; i < min_locals; ++i)
    {
        CB_VerificationType a = dest->locals[i];
        CB_VerificationType b = src->locals[i];
        bool a_wide = a.tag == CF_VERIFICATION_LONG || a.tag == CF_VERIFICATION_DOUBLE;
        bool b_wide = b.tag == CF_VERIFICATION_LONG || b.tag == CF_VERIFICATION_DOUBLE;

        if (a_wide || b_wide)
        {
            if (a_wide && b_wide && cb_type_equals(&a, &b))
            {
                dest->locals[i] = a;
            }
            else
            {
#ifdef DEBUG_FRAME
                if (cb_merge_verbose)
                {
                    fprintf(stderr, "[merge] locals[%d]: wide mismatch %s vs %s -> top\n",
                            i, cb_type_name(&a), cb_type_name(&b));
                }
#endif
                dest->locals[i] = cb_type_top();
            }
            if (i + 1 < CB_MAX_LOCALS)
            {
                dest->locals[i + 1] = cb_type_top();
            }
            i++;
            continue;
        }

        CB_VerificationType merged = cb_merge_type(a, b);
#ifdef DEBUG_FRAME
        if (cb_merge_verbose && merged.tag == CF_VERIFICATION_TOP &&
            (a.tag != CF_VERIFICATION_TOP || b.tag != CF_VERIFICATION_TOP))
        {
            fprintf(stderr, "[merge] locals[%d]: %s vs %s -> TOP (incompatible)\n",
                    i, cb_type_name(&a), cb_type_name(&b));
        }
#endif
        dest->locals[i] = merged;
    }

    /* Truncate to minimum: clear extra slots to TOP and set count */
    for (int i = min_locals; i < dest->locals_count; ++i)
    {
        dest->locals[i] = cb_type_top();
    }
    dest->locals_count = min_locals;
    dest->locals_count = cb_effective_locals_count(dest);

    if (dest->stack_count != src->stack_count)
    {
        /* This warning indicates a code generation bug where different control
         * flow paths to a merge point have different stack depths.
         * This can happen when:
         * 1. A label is reached via both fallthrough (with stack) and jump (empty stack)
         * 2. Short-circuit evaluation in logical OR/AND mixes paths incorrectly
         * Taking the smaller stack count to avoid overflow, but the generated
         * StackMapTable may be incorrect causing JVM verification errors. */
        fprintf(stderr, "codebuilder: stack depth mismatch at merge: dest=%d src=%d\n",
                dest->stack_count, src->stack_count);
        if (src->stack_count < dest->stack_count)
        {
            dest->stack_count = src->stack_count;
        }
    }

    for (int i = 0; i < dest->stack_count; ++i)
    {
        CB_VerificationType a = dest->stack[i];
        CB_VerificationType b = (i < src->stack_count)
                                    ? src->stack[i]
                                    : cb_type_top();
        bool a_wide = a.tag == CF_VERIFICATION_LONG || a.tag == CF_VERIFICATION_DOUBLE;
        bool b_wide = b.tag == CF_VERIFICATION_LONG || b.tag == CF_VERIFICATION_DOUBLE;

        if (a_wide || b_wide)
        {
            if (a_wide && b_wide && cb_type_equals(&a, &b))
            {
                dest->stack[i] = a;
            }
            else
            {
                dest->stack[i] = cb_type_top();
            }
            if (i + 1 < CB_MAX_STACK)
            {
                dest->stack[i + 1] = cb_type_top();
            }
            i++;
            continue;
        }

        dest->stack[i] = cb_merge_type(a, b);
    }
}

void codebuilder_restore_frame_safe(CodeBuilder *builder, const CB_Frame *saved)
{
    if (!builder || !saved)
    {
        return;
    }
    cb_copy_frame(builder->frame, saved);
    cb_update_max_stack(builder);
}

/* ============================================================
 * Max Tracking
 * ============================================================ */

void cb_update_max_stack(CodeBuilder *builder)
{
    if (!builder)
    {
        return;
    }

    if (builder->frame->stack_count > builder->max_stack)
    {
        builder->max_stack = builder->frame->stack_count;
    }
}

void cb_update_max_locals(CodeBuilder *builder, int index)
{
    if (!builder)
    {
        return;
    }

    if (index + 1 > builder->max_locals)
    {
        builder->max_locals = index + 1;
    }
}

/* ============================================================
 * Stack Operations
 * ============================================================ */

void cb_push(CodeBuilder *builder, CB_VerificationType type)
{
    if (!builder)
    {
        return;
    }

    /* Warn if operating on dead code */
    if (!builder->alive)
    {
        builder->diag_dead_code_op_count++;
        if (builder->diag_dead_code_op_count <= 3)
        {
            fprintf(stderr, "codebuilder: push in dead code at pc=%d in %s (stack=%d)\n",
                    codebuilder_current_pc(builder),
                    builder->method_name ? builder->method_name : "<unknown>",
                    builder->frame->stack_count);
        }
    }

    if (builder->frame->stack_count >= CB_MAX_STACK)
    {
        fprintf(stderr, "codebuilder: stack overflow\n");
        return;
    }

    builder->frame->stack[builder->frame->stack_count++] = type;

    /* For long/double, push TOP as second slot */
    if (cb_type_slots(&type) == 2)
    {
        if (builder->frame->stack_count >= CB_MAX_STACK)
        {
            fprintf(stderr, "codebuilder: stack overflow (second slot)\n");
            return;
        }
        builder->frame->stack[builder->frame->stack_count++] = cb_type_top();
    }

    cb_update_max_stack(builder);
}

CB_VerificationType cb_pop(CodeBuilder *builder)
{
    if (!builder)
    {
        return cb_type_top();
    }

    /* In dead code, stack operations are meaningless - return dummy value */
    if (!builder->alive)
    {
        /* For dead code like 'do { goto ...; } while (0)', the while condition
         * check generates ifne but no value was pushed. Just return top type. */
        if (builder->frame->stack_count == 0)
        {
            return cb_type_top();
        }
    }

    if (builder->frame->stack_count == 0)
    {
        builder->diag_stack_underflow_count++;
        fprintf(stderr, "codebuilder: stack underflow at pc=%d in %s\n",
                codebuilder_current_pc(builder),
                builder->method_name ? builder->method_name : "<unknown>");
        return cb_type_top();
    }

    CB_VerificationType top = builder->frame->stack[--builder->frame->stack_count];

    /* If we popped a TOP that is second slot of long/double, also pop the actual type */
    if (top.tag == CF_VERIFICATION_TOP && builder->frame->stack_count > 0)
    {
        CB_VerificationType prev = builder->frame->stack[builder->frame->stack_count - 1];
        if (prev.tag == CF_VERIFICATION_LONG || prev.tag == CF_VERIFICATION_DOUBLE)
        {
            top = builder->frame->stack[--builder->frame->stack_count];
        }
    }

    cb_update_max_stack(builder);
    return top;
}

void cb_set_stack_depth(CodeBuilder *builder, int depth)
{
    if (!builder)
    {
        return;
    }

    /* Set stack depth directly (for control flow, e.g., after goto/return) */
    if (depth > CB_MAX_STACK)
    {
        depth = CB_MAX_STACK;
    }

    builder->frame->stack_count = (uint16_t)depth;
    cb_update_max_stack(builder);
}

void codebuilder_set_stack(CodeBuilder *builder, int value)
{
    if (!builder)
    {
        return;
    }

    int current = builder->frame->stack_count;
    if (value > current)
    {
        fprintf(stderr, "codebuilder: stack depth restore cannot grow: %u -> %u\n",
                current, value);
        return;
    }

    /* Pop items to reach target depth */
    while (builder->frame->stack_count > value)
    {
        builder->frame->stack_count--;
    }

    cb_update_max_stack(builder);
}

int codebuilder_current_stack(CodeBuilder *builder)
{
    if (!builder)
    {
        return 0;
    }

    return builder->frame->stack_count;
}

CodebuilderStackMark codebuilder_mark_stack(CodeBuilder *builder)
{
    CodebuilderStackMark mark = {};

    if (!builder)
    {
        return mark;
    }

    mark.frame = cb_create_frame();
    cb_copy_frame(mark.frame, builder->frame);
    mark.stack_depth = builder->frame->stack_count;
    return mark;
}

void codebuilder_restore_stack(CodeBuilder *builder, CodebuilderStackMark mark)
{
    if (!builder)
    {
        return;
    }

    codebuilder_restore_frame_safe(builder, mark.frame);
    /* Free the frame owned by the mark */
    if (mark.frame)
    {
        free(mark.frame);
    }
}

/* ============================================================
 * Block Scope Operations (Javac-style)
 * ============================================================ */

void codebuilder_begin_block(CodeBuilder *builder)
{
    if (!builder)
    {
        return;
    }

    if (builder->block_depth >= CB_MAX_SCOPE_DEPTH)
    {
        fprintf(stderr, "codebuilder: block depth exceeds maximum\n");
        return;
    }

    /* Save current locals count for restoration at block exit */
    builder->block_locals_base[builder->block_depth] = builder->frame->locals_count;
    builder->block_depth++;
}

void codebuilder_end_block(CodeBuilder *builder)
{
    if (!builder)
    {
        return;
    }

    if (builder->block_depth == 0)
    {
        fprintf(stderr, "codebuilder: block depth underflow\n");
        return;
    }

    builder->block_depth--;
    int saved_locals = builder->block_locals_base[builder->block_depth];

    /* Reset locals to allow slot reuse, but keep max_locals unchanged */
    if (builder->frame->locals_count > saved_locals)
    {
        /* Clear types for reused slots */
        for (int i = saved_locals; i < builder->frame->locals_count; ++i)
        {
            builder->frame->locals[i] = cb_type_top();
        }
        builder->frame->locals_count = saved_locals;
    }
}

int codebuilder_allocate_local(CodeBuilder *builder, CB_VerificationType type)
{
    if (!builder)
    {
        return 0;
    }

    int index = builder->frame->locals_count;
    int slots = cb_type_slots(&type);

    if (index + slots > CB_MAX_LOCALS)
    {
        fprintf(stderr, "codebuilder: too many locals in %s (current: %d, max: %d)\n",
                builder->method_name ? builder->method_name : "<unknown>",
                index + slots, CB_MAX_LOCALS);
        return 0;
    }

    /* Set type at allocated slot */
    builder->frame->locals[index] = type;
    builder->frame->locals_count += slots;

    /* For long/double, set second slot to TOP */
    if (slots == 2)
    {
        builder->frame->locals[index + 1] = cb_type_top();
    }

    /* Update max_locals */
    cb_update_max_locals(builder, index + slots - 1);

    return index;
}

int codebuilder_current_locals(CodeBuilder *builder)
{
    if (!builder)
    {
        return 0;
    }
    return builder->frame->locals_count;
}

/* ============================================================
 * Local Variable Operations
 * ============================================================ */

void codebuilder_set_local(CodeBuilder *builder, int index, CB_VerificationType type)
{
    if (!builder)
    {
        return;
    }

    if (index >= CB_MAX_LOCALS)
    {
        fprintf(stderr, "codebuilder: local index %d exceeds maximum\n", index);
        return;
    }

    builder->frame->locals[index] = type;

    /* Update locals_count if needed */
    int slots = cb_type_slots(&type);
    int end_index = index + slots;
    if (end_index > builder->frame->locals_count)
    {
        builder->frame->locals_count = end_index;
    }

    /* For long/double, set the second slot to TOP */
    if (slots == 2 && index + 1 < CB_MAX_LOCALS)
    {
        builder->frame->locals[index + 1] = cb_type_top();
    }

    cb_update_max_locals(builder, index + slots - 1);
}

void codebuilder_set_param(CodeBuilder *builder, int index, CB_VerificationType type)
{
    if (!builder)
    {
        return;
    }

    /* Set in both frame and initial_frame for parameters */
    codebuilder_set_local(builder, index, type);

    /* Also update initial_frame for correct StackMapTable generation */
    if (index < CB_MAX_LOCALS)
    {
        builder->initial_frame->locals[index] = type;
        int slots = cb_type_slots(&type);
        int end_index = index + slots;
        if (end_index > builder->initial_frame->locals_count)
        {
            builder->initial_frame->locals_count = end_index;
        }
        if (slots == 2 && index + 1 < CB_MAX_LOCALS)
        {
            builder->initial_frame->locals[index + 1] = cb_type_top();
        }
    }
}

CB_VerificationType codebuilder_get_local(CodeBuilder *builder, int index)
{
    if (!builder || index >= CB_MAX_LOCALS)
    {
        return cb_type_top();
    }
    return builder->frame->locals[index];
}

/* ============================================================
 * Diagnostics
 * ============================================================ */

void codebuilder_print_diagnostics(CodeBuilder *builder)
{
    if (!builder)
    {
        return;
    }

    int total = builder->diag_stack_underflow_count +
                builder->diag_stack_mismatch_count +
                builder->diag_dead_code_op_count;

    if (total == 0)
    {
        return;
    }

    fprintf(stderr, "codebuilder diagnostics for %s:\n",
            builder->method_name ? builder->method_name : "<unknown>");

    if (builder->diag_stack_underflow_count > 0)
    {
        fprintf(stderr, "  stack underflow: %d\n", builder->diag_stack_underflow_count);
    }
    if (builder->diag_stack_mismatch_count > 0)
    {
        fprintf(stderr, "  stack mismatch: %d\n", builder->diag_stack_mismatch_count);
    }
    if (builder->diag_dead_code_op_count > 0)
    {
        fprintf(stderr, "  dead code ops: %d\n", builder->diag_dead_code_op_count);
    }
}

bool codebuilder_has_errors(CodeBuilder *builder)
{
    if (!builder)
    {
        return false;
    }

    return builder->diag_stack_underflow_count > 0 ||
           builder->diag_stack_mismatch_count > 0;
}
