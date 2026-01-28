#include <stdio.h>
#include <stdlib.h>

#include "codebuilder_part2.h"
#include "codebuilder_internal.h"
#include "codebuilder_types.h"
#include "classfile_opcode.h"

/* ============================================================
 * Category Check Helpers
 * ============================================================
 * JVM category rules:
 * - Category 1: int, float, reference, etc. (1 slot)
 * - Category 2: long, double (2 slots)
 */

static bool cb_is_category1(CB_VerificationType *type)
{
    if (!type)
    {
        fprintf(stderr, "cb_is_category1: type is NULL\n");
        exit(1);
    }
    return cb_type_slots(type) == 1;
}

static bool cb_is_category2(CB_VerificationType *type)
{
    if (!type)
    {
        fprintf(stderr, "cb_is_category2: type is NULL\n");
        exit(1);
    }
    return cb_type_slots(type) == 2;
}

/* Get stack top type (without popping) */
static CB_VerificationType *cb_peek_top(CodeBuilder *builder)
{
    if (!builder || builder->frame->stack_count == 0)
    {
        return NULL;
    }
    /* Check if top is TOP (second slot of category 2) */
    CB_VerificationType *top = &builder->frame->stack[builder->frame->stack_count - 1];
    if (top->tag == CF_VERIFICATION_TOP && builder->frame->stack_count > 1)
    {
        CB_VerificationType *prev = &builder->frame->stack[builder->frame->stack_count - 2];
        if (prev->tag == CF_VERIFICATION_LONG || prev->tag == CF_VERIFICATION_DOUBLE)
        {
            return prev;
        }
    }
    return top;
}

/* Get second-from-top type (without popping) */
static CB_VerificationType *cb_peek_second(CodeBuilder *builder)
{
    if (!builder)
    {
        return NULL;
    }
    CB_VerificationType *top = cb_peek_top(builder);
    int top_slots = top ? cb_type_slots(top) : 1;
    int second_idx = builder->frame->stack_count - top_slots - 1;
    if (second_idx >= builder->frame->stack_count)
    {
        return NULL;
    }
    CB_VerificationType *second = &builder->frame->stack[second_idx];
    /* Handle TOP marker for category 2 */
    if (second->tag == CF_VERIFICATION_TOP && second_idx > 0)
    {
        CB_VerificationType *prev = &builder->frame->stack[second_idx - 1];
        if (prev->tag == CF_VERIFICATION_LONG || prev->tag == CF_VERIFICATION_DOUBLE)
        {
            return prev;
        }
    }
    return second;
}

/* ============================================================
 * Low-Level Stack Operations (with category checks)
 * ============================================================ */

void codebuilder_build_pop(CodeBuilder *builder)
{
    /* pop: requires category 1 on top */
    CB_VerificationType *top = cb_peek_top(builder);
    if (!cb_is_category1(top))
    {
        fprintf(stderr, "codebuilder: pop requires category 1, got category 2\n");
        exit(1);
    }
    classfile_opcode_emit_pop(builder->method);
    cb_pop(builder);
}

void codebuilder_build_pop2(CodeBuilder *builder)
{
    classfile_opcode_emit_pop2(builder->method);
    CB_VerificationType top = cb_pop(builder);
    if (cb_type_slots(&top) == 1)
    {
        cb_pop(builder);
    }
}

void codebuilder_build_pop_value(CodeBuilder *builder)
{
    if (builder->frame->stack_count == 0)
    {
        return;
    }
    CB_VerificationType top = builder->frame->stack[builder->frame->stack_count - 1];

    /* Check if top is TOP (second slot of long/double) */
    if (top.tag == CF_VERIFICATION_TOP && builder->frame->stack_count > 1)
    {
        CB_VerificationType prev = builder->frame->stack[builder->frame->stack_count - 2];
        if (prev.tag == CF_VERIFICATION_LONG || prev.tag == CF_VERIFICATION_DOUBLE)
        {
            codebuilder_build_pop2(builder);
            return;
        }
    }

    if (cb_type_slots(&top) == 2)
    {
        codebuilder_build_pop2(builder);
    }
    else
    {
        codebuilder_build_pop(builder);
    }
}

void codebuilder_build_dup(CodeBuilder *builder)
{
    /* dup: requires category 1 on top */
    CB_VerificationType *top = cb_peek_top(builder);
    if (!cb_is_category1(top))
    {
        fprintf(stderr, "codebuilder: dup requires category 1, got category 2\n");
        exit(1);
    }
    classfile_opcode_emit_dup(builder->method);
    CB_VerificationType popped = cb_pop(builder);
    cb_push(builder, popped);
    cb_push(builder, popped);
}

void codebuilder_build_dup_x1(CodeBuilder *builder)
{
    /* dup_x1: requires category 1 x 2 */
    CB_VerificationType *top = cb_peek_top(builder);
    CB_VerificationType *second = cb_peek_second(builder);
    if (!cb_is_category1(top) || !cb_is_category1(second))
    {
        fprintf(stderr, "codebuilder: dup_x1 requires category 1 x 2\n");
        exit(1);
    }
    classfile_opcode_emit_dup_x1(builder->method);
    CB_VerificationType value1 = cb_pop(builder);
    CB_VerificationType value2 = cb_pop(builder);
    cb_push(builder, value1);
    cb_push(builder, value2);
    cb_push(builder, value1);
}

void codebuilder_build_dup_x2(CodeBuilder *builder)
{
    /* dup_x2: value1 must be category 1
     * Form 1: value1(cat1), value2(cat1), value3(cat1)
     * Form 2: value1(cat1), value2(cat2)
     */
    CB_VerificationType *top = cb_peek_top(builder);
    if (!cb_is_category1(top))
    {
        fprintf(stderr, "codebuilder: dup_x2 requires value1 to be category 1\n");
        exit(1);
    }
    classfile_opcode_emit_dup_x2(builder->method);
    CB_VerificationType value1 = cb_pop(builder);
    CB_VerificationType value2 = cb_pop(builder);
    CB_VerificationType value3 = cb_pop(builder);
    cb_push(builder, value1);
    cb_push(builder, value3);
    cb_push(builder, value2);
    cb_push(builder, value1);
}

void codebuilder_build_dup2(CodeBuilder *builder)
{
    classfile_opcode_emit_dup2(builder->method);
    CB_VerificationType value1 = cb_pop(builder);
    if (cb_type_slots(&value1) == 2)
    {
        cb_push(builder, value1);
        cb_push(builder, value1);
        return;
    }

    CB_VerificationType value2 = cb_pop(builder);
    cb_push(builder, value2);
    cb_push(builder, value1);
    cb_push(builder, value2);
    cb_push(builder, value1);
}

void codebuilder_build_dup2_x1(CodeBuilder *builder)
{
    classfile_opcode_emit_dup2_x1(builder->method);
    CB_VerificationType value1 = cb_pop(builder);
    if (cb_type_slots(&value1) == 2)
    {
        CB_VerificationType value2 = cb_pop(builder);
        cb_push(builder, value1);
        cb_push(builder, value2);
        cb_push(builder, value1);
        return;
    }

    CB_VerificationType value2 = cb_pop(builder);
    CB_VerificationType value3 = cb_pop(builder);
    cb_push(builder, value2);
    cb_push(builder, value1);
    cb_push(builder, value3);
    cb_push(builder, value2);
    cb_push(builder, value1);
}

void codebuilder_build_dup2_x2(CodeBuilder *builder)
{
    classfile_opcode_emit_dup2_x2(builder->method);
    CB_VerificationType value1 = cb_pop(builder);
    if (cb_type_slots(&value1) == 2)
    {
        CB_VerificationType value2 = cb_pop(builder);
        if (cb_type_slots(&value2) == 2)
        {
            cb_push(builder, value1);
            cb_push(builder, value2);
            cb_push(builder, value1);
            return;
        }

        CB_VerificationType value3 = cb_pop(builder);
        cb_push(builder, value1);
        cb_push(builder, value3);
        cb_push(builder, value2);
        cb_push(builder, value1);
        return;
    }

    CB_VerificationType value2 = cb_pop(builder);
    CB_VerificationType value3 = cb_pop(builder);
    if (cb_type_slots(&value3) == 2)
    {
        cb_push(builder, value2);
        cb_push(builder, value1);
        cb_push(builder, value3);
        cb_push(builder, value2);
        cb_push(builder, value1);
        return;
    }

    CB_VerificationType value4 = cb_pop(builder);
    cb_push(builder, value2);
    cb_push(builder, value1);
    cb_push(builder, value4);
    cb_push(builder, value3);
    cb_push(builder, value2);
    cb_push(builder, value1);
}

void codebuilder_build_swap(CodeBuilder *builder)
{
    /* swap: requires category 1 x 2 */
    CB_VerificationType *top = cb_peek_top(builder);
    CB_VerificationType *second = cb_peek_second(builder);
    if (!cb_is_category1(top) || !cb_is_category1(second))
    {
        fprintf(stderr, "codebuilder: swap requires category 1 x 2\n");
        exit(1);
    }
    classfile_opcode_emit_swap(builder->method);
    CB_VerificationType value1 = cb_pop(builder);
    CB_VerificationType value2 = cb_pop(builder);
    cb_push(builder, value1);
    cb_push(builder, value2);
}

void codebuilder_build_iadd(CodeBuilder *builder)
{
    classfile_opcode_emit_iadd(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_int());
}

void codebuilder_build_ladd(CodeBuilder *builder)
{
    classfile_opcode_emit_ladd(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_long());
}

void codebuilder_build_fadd(CodeBuilder *builder)
{
    classfile_opcode_emit_fadd(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_float());
}

void codebuilder_build_dadd(CodeBuilder *builder)
{
    classfile_opcode_emit_dadd(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_double());
}

void codebuilder_build_isub(CodeBuilder *builder)
{
    classfile_opcode_emit_isub(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_int());
}

void codebuilder_build_lsub(CodeBuilder *builder)
{
    classfile_opcode_emit_lsub(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_long());
}

void codebuilder_build_fsub(CodeBuilder *builder)
{
    classfile_opcode_emit_fsub(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_float());
}

void codebuilder_build_dsub(CodeBuilder *builder)
{
    classfile_opcode_emit_dsub(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_double());
}

void codebuilder_build_imul(CodeBuilder *builder)
{
    classfile_opcode_emit_imul(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_int());
}

void codebuilder_build_lmul(CodeBuilder *builder)
{
    classfile_opcode_emit_lmul(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_long());
}

void codebuilder_build_fmul(CodeBuilder *builder)
{
    classfile_opcode_emit_fmul(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_float());
}

void codebuilder_build_dmul(CodeBuilder *builder)
{
    classfile_opcode_emit_dmul(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_double());
}

void codebuilder_build_idiv(CodeBuilder *builder)
{
    classfile_opcode_emit_idiv(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_int());
}

void codebuilder_build_ldiv(CodeBuilder *builder)
{
    classfile_opcode_emit_ldiv(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_long());
}

void codebuilder_build_fdiv(CodeBuilder *builder)
{
    classfile_opcode_emit_fdiv(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_float());
}

void codebuilder_build_ddiv(CodeBuilder *builder)
{
    classfile_opcode_emit_ddiv(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_double());
}

void codebuilder_build_irem(CodeBuilder *builder)
{
    classfile_opcode_emit_irem(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_int());
}

void codebuilder_build_lrem(CodeBuilder *builder)
{
    classfile_opcode_emit_lrem(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_long());
}

void codebuilder_build_frem(CodeBuilder *builder)
{
    classfile_opcode_emit_frem(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_float());
}

void codebuilder_build_drem(CodeBuilder *builder)
{
    classfile_opcode_emit_drem(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_double());
}

void codebuilder_build_ineg(CodeBuilder *builder)
{
    classfile_opcode_emit_ineg(builder->method);
    cb_pop(builder);
    cb_push(builder, cb_type_int());
}

void codebuilder_build_lneg(CodeBuilder *builder)
{
    classfile_opcode_emit_lneg(builder->method);
    cb_pop(builder);
    cb_push(builder, cb_type_long());
}

void codebuilder_build_fneg(CodeBuilder *builder)
{
    classfile_opcode_emit_fneg(builder->method);
    cb_pop(builder);
    cb_push(builder, cb_type_float());
}

void codebuilder_build_dneg(CodeBuilder *builder)
{
    classfile_opcode_emit_dneg(builder->method);
    cb_pop(builder);
    cb_push(builder, cb_type_double());
}

void codebuilder_build_ishl(CodeBuilder *builder)
{
    classfile_opcode_emit_ishl(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_int());
}

void codebuilder_build_lshl(CodeBuilder *builder)
{
    classfile_opcode_emit_lshl(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_long());
}

void codebuilder_build_ishr(CodeBuilder *builder)
{
    classfile_opcode_emit_ishr(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_int());
}

void codebuilder_build_lshr(CodeBuilder *builder)
{
    classfile_opcode_emit_lshr(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_long());
}

void codebuilder_build_iushr(CodeBuilder *builder)
{
    classfile_opcode_emit_iushr(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_int());
}

void codebuilder_build_lushr(CodeBuilder *builder)
{
    classfile_opcode_emit_lushr(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_long());
}

void codebuilder_build_iand(CodeBuilder *builder)
{
    classfile_opcode_emit_iand(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_int());
}

void codebuilder_build_land(CodeBuilder *builder)
{
    classfile_opcode_emit_land(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_long());
}

void codebuilder_build_ior(CodeBuilder *builder)
{
    classfile_opcode_emit_ior(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_int());
}

void codebuilder_build_lor(CodeBuilder *builder)
{
    classfile_opcode_emit_lor(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_long());
}

void codebuilder_build_ixor(CodeBuilder *builder)
{
    classfile_opcode_emit_ixor(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_int());
}

void codebuilder_build_lxor(CodeBuilder *builder)
{
    classfile_opcode_emit_lxor(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_long());
}

void codebuilder_build_iinc(CodeBuilder *builder, int local_index, int increment)
{
    classfile_opcode_emit_iinc(builder->method, local_index, increment);
}

void codebuilder_build_i2l(CodeBuilder *builder)
{
    classfile_opcode_emit_i2l(builder->method);
    cb_pop(builder);
    cb_push(builder, cb_type_long());
}

void codebuilder_build_i2f(CodeBuilder *builder)
{
    classfile_opcode_emit_i2f(builder->method);
    cb_pop(builder);
    cb_push(builder, cb_type_float());
}

void codebuilder_build_i2d(CodeBuilder *builder)
{
    classfile_opcode_emit_i2d(builder->method);
    cb_pop(builder);
    cb_push(builder, cb_type_double());
}

void codebuilder_build_l2i(CodeBuilder *builder)
{
    classfile_opcode_emit_l2i(builder->method);
    cb_pop(builder);
    cb_push(builder, cb_type_int());
}

void codebuilder_build_l2f(CodeBuilder *builder)
{
    classfile_opcode_emit_l2f(builder->method);
    cb_pop(builder);
    cb_push(builder, cb_type_float());
}

void codebuilder_build_l2d(CodeBuilder *builder)
{
    classfile_opcode_emit_l2d(builder->method);
    cb_pop(builder);
    cb_push(builder, cb_type_double());
}

void codebuilder_build_f2i(CodeBuilder *builder)
{
    classfile_opcode_emit_f2i(builder->method);
    cb_pop(builder);
    cb_push(builder, cb_type_int());
}

void codebuilder_build_f2l(CodeBuilder *builder)
{
    classfile_opcode_emit_f2l(builder->method);
    cb_pop(builder);
    cb_push(builder, cb_type_long());
}

void codebuilder_build_f2d(CodeBuilder *builder)
{
    classfile_opcode_emit_f2d(builder->method);
    cb_pop(builder);
    cb_push(builder, cb_type_double());
}

void codebuilder_build_d2i(CodeBuilder *builder)
{
    classfile_opcode_emit_d2i(builder->method);
    cb_pop(builder);
    cb_push(builder, cb_type_int());
}

void codebuilder_build_d2l(CodeBuilder *builder)
{
    classfile_opcode_emit_d2l(builder->method);
    cb_pop(builder);
    cb_push(builder, cb_type_long());
}

void codebuilder_build_d2f(CodeBuilder *builder)
{
    classfile_opcode_emit_d2f(builder->method);
    cb_pop(builder);
    cb_push(builder, cb_type_float());
}

void codebuilder_build_i2b(CodeBuilder *builder)
{
    classfile_opcode_emit_i2b(builder->method);
    cb_pop(builder);
    cb_push(builder, cb_type_int());
}

void codebuilder_build_i2c(CodeBuilder *builder)
{
    classfile_opcode_emit_i2c(builder->method);
    cb_pop(builder);
    cb_push(builder, cb_type_int());
}

void codebuilder_build_i2s(CodeBuilder *builder)
{
    classfile_opcode_emit_i2s(builder->method);
    cb_pop(builder);
    cb_push(builder, cb_type_int());
}

void codebuilder_build_lcmp(CodeBuilder *builder)
{
    classfile_opcode_emit_lcmp(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_int());
}

void codebuilder_build_fcmp(CodeBuilder *builder, CmpNaN nan_behavior)
{
    classfile_opcode_emit_fcmp(builder->method, nan_behavior);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_int());
}

void codebuilder_build_dcmp(CodeBuilder *builder, CmpNaN nan_behavior)
{
    classfile_opcode_emit_dcmp(builder->method, nan_behavior);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_int());
}

/* ============================================================
 * High-Level Stack Operations (semantic APIs)
 * ============================================================
 * These APIs automatically select the correct JVM instruction
 * based on the stack state. Use these instead of raw dup/dup2/etc
 * when operating on "values" rather than raw stack slots.
 */

void codebuilder_build_dup_value(CodeBuilder *builder)
{
    /* Duplicate top value: cat1 -> dup, cat2 -> dup2 */
    CB_VerificationType *top = cb_peek_top(builder);
    if (cb_is_category2(top))
    {
        codebuilder_build_dup2(builder);
    }
    else
    {
        codebuilder_build_dup(builder);
    }
}

void codebuilder_build_dup_address(CodeBuilder *builder)
{
    /* Duplicate array element address (arrayref + index).
     * This is always two category 1 values, so use dup2.
     */
    CB_VerificationType *top = cb_peek_top(builder);
    CB_VerificationType *second = cb_peek_second(builder);
    if (!cb_is_category1(top) || !cb_is_category1(second))
    {
        fprintf(stderr, "codebuilder: dup_address requires cat1 x 2 (arrayref + index)\n");
        exit(1);
    }
    codebuilder_build_dup2(builder);
}

void codebuilder_build_dup_value_x1(CodeBuilder *builder)
{
    /* Duplicate top value and insert below second value.
     * cat1 value, cat1 below -> dup_x1
     * cat2 value, cat1 below -> dup2_x1
     */
    CB_VerificationType *top = cb_peek_top(builder);
    if (cb_is_category2(top))
    {
        codebuilder_build_dup2_x1(builder);
    }
    else
    {
        codebuilder_build_dup_x1(builder);
    }
}

void codebuilder_build_dup_value_x2(CodeBuilder *builder)
{
    /* Duplicate top value and insert below two values (or one cat2).
     * cat1 value -> dup_x2
     * cat2 value -> dup2_x2
     */
    CB_VerificationType *top = cb_peek_top(builder);
    if (cb_is_category2(top))
    {
        codebuilder_build_dup2_x2(builder);
    }
    else
    {
        codebuilder_build_dup_x2(builder);
    }
}
