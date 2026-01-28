#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "codebuilder_part1.h"
#include "codebuilder_internal.h"
#include "codebuilder_frame.h"
#include "codebuilder_types.h"
#include "codebuilder_label.h"
#include "classfile_opcode.h"

static int codebuilder_add_constant_int(CodeBuilder *builder, int32_t value)
{
    return cp_builder_add_int(builder->cp, value);
}

static int codebuilder_add_constant_long(CodeBuilder *builder, int64_t value)
{
    return cp_builder_add_long(builder->cp, value);
}

static int codebuilder_add_constant_float(CodeBuilder *builder, float value)
{
    return cp_builder_add_float(builder->cp, value);
}

static int codebuilder_add_constant_double(CodeBuilder *builder, double value)
{
    return cp_builder_add_double(builder->cp, value);
}

void codebuilder_build_nop(CodeBuilder *builder)
{
    classfile_opcode_emit_nop(builder->method);
}

void codebuilder_build_aconst_null(CodeBuilder *builder)
{
    classfile_opcode_emit_aconst_null(builder->method);
    cb_push(builder, cb_type_null());
}

void codebuilder_build_iconst(CodeBuilder *builder, int32_t value)
{
    if (value >= -1 && value <= 5)
    {
        classfile_opcode_emit_iconst(builder->method, value);
        cb_push(builder, cb_type_int());
        return;
    }

    if (value >= INT8_MIN && value <= INT8_MAX)
    {
        classfile_opcode_emit_bipush(builder->method, value);
        cb_push(builder, cb_type_int());
        return;
    }

    if (value >= INT16_MIN && value <= INT16_MAX)
    {
        classfile_opcode_emit_sipush(builder->method, value);
        cb_push(builder, cb_type_int());
        return;
    }

    int idx = codebuilder_add_constant_int(builder, value);
    if (idx > 255)
        classfile_opcode_emit_ldc_w(builder->method, idx);
    else
        classfile_opcode_emit_ldc(builder->method, idx);
    cb_push(builder, cb_type_int());
}

void codebuilder_build_lconst(CodeBuilder *builder, long value)
{
    if (value == 0 || value == 1)
    {
        classfile_opcode_emit_lconst(builder->method, value);
        cb_push(builder, cb_type_long());
        return;
    }

    int idx = codebuilder_add_constant_long(builder, value);
    classfile_opcode_emit_ldc2_w(builder->method, idx);
    cb_push(builder, cb_type_long());
}

void codebuilder_build_fconst(CodeBuilder *builder, float value)
{
    if (value == 0.0f || value == 1.0f || value == 2.0f)
    {
        classfile_opcode_emit_fconst(builder->method, value);
        cb_push(builder, cb_type_float());
        return;
    }

    int idx = codebuilder_add_constant_float(builder, value);
    if (idx > 255)
        classfile_opcode_emit_ldc_w(builder->method, idx);
    else
        classfile_opcode_emit_ldc(builder->method, idx);
    cb_push(builder, cb_type_float());
}

void codebuilder_build_dconst(CodeBuilder *builder, double value)
{
    if (value == 0.0 || value == 1.0)
    {
        classfile_opcode_emit_dconst(builder->method, value);
        cb_push(builder, cb_type_double());
        return;
    }

    int idx = codebuilder_add_constant_double(builder, value);
    classfile_opcode_emit_ldc2_w(builder->method, idx);
    cb_push(builder, cb_type_double());
}

void codebuilder_build_ldc(CodeBuilder *builder, int index, CF_ValueTag tag)
{
    if (index > 255)
        classfile_opcode_emit_ldc_w(builder->method, index);
    else
        classfile_opcode_emit_ldc(builder->method, index);
    cb_push(builder, cb_type_from_value_tag(tag));
}

void codebuilder_build_ldc2_w(CodeBuilder *builder, int index, CF_ValueTag tag)
{
    classfile_opcode_emit_ldc2_w(builder->method, index);
    cb_push(builder, cb_type_from_value_tag(tag));
}

void codebuilder_build_iload(CodeBuilder *builder, int index)
{
    /* Verify local is integer type */
    CB_VerificationType local_type = codebuilder_get_local(builder, (uint16_t)index);
    if (!cb_type_is_integer(&local_type))
    {
        fprintf(stderr, "codebuilder: type error at pc=%d in %s: iload expects integer at local[%d], got %s\n",
                codebuilder_current_pc(builder),
                builder->method_name ? builder->method_name : "<unknown>",
                index, cb_type_name(&local_type));
    }
    classfile_opcode_emit_iload(builder->method, index);
    cb_push(builder, cb_type_int());
}

void codebuilder_build_lload(CodeBuilder *builder, int index)
{
    /* Verify local is long type */
    CB_VerificationType local_type = codebuilder_get_local(builder, (uint16_t)index);
    if (local_type.tag != CF_VERIFICATION_LONG)
    {
        fprintf(stderr, "codebuilder: type error at pc=%d in %s: lload expects long at local[%d], got %s\n",
                codebuilder_current_pc(builder),
                builder->method_name ? builder->method_name : "<unknown>",
                index, cb_type_name(&local_type));
    }
    classfile_opcode_emit_lload(builder->method, index);
    cb_push(builder, cb_type_long());
}

void codebuilder_build_fload(CodeBuilder *builder, int index)
{
    /* Verify local is float type */
    CB_VerificationType local_type = codebuilder_get_local(builder, (uint16_t)index);
    if (local_type.tag != CF_VERIFICATION_FLOAT)
    {
        fprintf(stderr, "codebuilder: type error at pc=%d in %s: fload expects float at local[%d], got %s\n",
                codebuilder_current_pc(builder),
                builder->method_name ? builder->method_name : "<unknown>",
                index, cb_type_name(&local_type));
    }
    classfile_opcode_emit_fload(builder->method, index);
    cb_push(builder, cb_type_float());
}

void codebuilder_build_dload(CodeBuilder *builder, int index)
{
    /* Verify local is double type */
    CB_VerificationType local_type = codebuilder_get_local(builder, (uint16_t)index);
    if (local_type.tag != CF_VERIFICATION_DOUBLE)
    {
        fprintf(stderr, "codebuilder: type error at pc=%d in %s: dload expects double at local[%d], got %s\n",
                codebuilder_current_pc(builder),
                builder->method_name ? builder->method_name : "<unknown>",
                index, cb_type_name(&local_type));
    }
    classfile_opcode_emit_dload(builder->method, index);
    cb_push(builder, cb_type_double());
}

void codebuilder_build_aload(CodeBuilder *builder, int index)
{
    /* Verify local is reference type */
    CB_VerificationType local_type = codebuilder_get_local(builder, (uint16_t)index);
    if (!cb_type_is_reference(&local_type))
    {
        fprintf(stderr, "codebuilder: type error at pc=%d in %s: aload expects reference at local[%d], got %s\n",
                codebuilder_current_pc(builder),
                builder->method_name ? builder->method_name : "<unknown>",
                index, cb_type_name(&local_type));
    }
    classfile_opcode_emit_aload(builder->method, index);
    /* Push the actual type from the local variable */
    cb_push(builder, local_type);
}

void codebuilder_build_iaload(CodeBuilder *builder)
{
    classfile_opcode_emit_iaload(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_int());
}

void codebuilder_build_laload(CodeBuilder *builder)
{
    classfile_opcode_emit_laload(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_long());
}

void codebuilder_build_faload(CodeBuilder *builder)
{
    classfile_opcode_emit_faload(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_float());
}

void codebuilder_build_daload(CodeBuilder *builder)
{
    classfile_opcode_emit_daload(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_double());
}

void codebuilder_build_aaload(CodeBuilder *builder)
{
    classfile_opcode_emit_aaload(builder->method);
    cb_pop(builder);                                  /* pop index */
    CB_VerificationType array_type = cb_pop(builder); /* pop arrayref */
    /* Extract element type from array type */
    CB_VerificationType element_type = cb_type_array_element(array_type);
    cb_push(builder, element_type);
}

void codebuilder_build_baload(CodeBuilder *builder)
{
    classfile_opcode_emit_baload(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_int());
}

void codebuilder_build_caload(CodeBuilder *builder)
{
    classfile_opcode_emit_caload(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_int());
}

void codebuilder_build_saload(CodeBuilder *builder)
{
    classfile_opcode_emit_saload(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_push(builder, cb_type_int());
}

void codebuilder_build_istore(CodeBuilder *builder, int index)
{
    /* Verify stack top is integer type */
    if (builder->frame->stack_count > 0)
    {
        CB_VerificationType *top = &builder->frame->stack[builder->frame->stack_count - 1];
        if (!cb_type_is_integer(top))
        {
            fprintf(stderr, "codebuilder: type error at pc=%d in %s: istore expects integer, got %s\n",
                    codebuilder_current_pc(builder),
                    builder->method_name ? builder->method_name : "<unknown>",
                    cb_type_name(top));
        }
    }
    classfile_opcode_emit_istore(builder->method, index);
    cb_pop(builder);
    codebuilder_set_local(builder, index, cb_type_int());
}

void codebuilder_build_lstore(CodeBuilder *builder, int index)
{
    /* Verify stack top is long type */
    if (builder->frame->stack_count >= 2)
    {
        CB_VerificationType *top = &builder->frame->stack[builder->frame->stack_count - 2];
        if (top->tag != CF_VERIFICATION_LONG)
        {
            fprintf(stderr, "codebuilder: type error at pc=%d in %s: lstore expects long, got %s\n",
                    codebuilder_current_pc(builder),
                    builder->method_name ? builder->method_name : "<unknown>",
                    cb_type_name(top));
        }
    }
    classfile_opcode_emit_lstore(builder->method, index);
    cb_pop(builder);
    codebuilder_set_local(builder, index, cb_type_long());
}

void codebuilder_build_fstore(CodeBuilder *builder, int index)
{
    /* Verify stack top is float type */
    if (builder->frame->stack_count > 0)
    {
        CB_VerificationType *top = &builder->frame->stack[builder->frame->stack_count - 1];
        if (top->tag != CF_VERIFICATION_FLOAT)
        {
            fprintf(stderr, "codebuilder: type error at pc=%d in %s: fstore expects float, got %s\n",
                    codebuilder_current_pc(builder),
                    builder->method_name ? builder->method_name : "<unknown>",
                    cb_type_name(top));
        }
    }
    classfile_opcode_emit_fstore(builder->method, index);
    cb_pop(builder);
    codebuilder_set_local(builder, index, cb_type_float());
}

void codebuilder_build_dstore(CodeBuilder *builder, int index)
{
    /* Verify stack top is double type */
    if (builder->frame->stack_count >= 2)
    {
        CB_VerificationType *top = &builder->frame->stack[builder->frame->stack_count - 2];
        if (top->tag != CF_VERIFICATION_DOUBLE)
        {
            fprintf(stderr, "codebuilder: type error at pc=%d in %s: dstore expects double, got %s\n",
                    codebuilder_current_pc(builder),
                    builder->method_name ? builder->method_name : "<unknown>",
                    cb_type_name(top));
        }
    }
    classfile_opcode_emit_dstore(builder->method, index);
    cb_pop(builder);
    codebuilder_set_local(builder, index, cb_type_double());
}

void codebuilder_build_astore(CodeBuilder *builder, int index)
{
    /* Verify stack top is reference type */
    if (builder->frame->stack_count > 0)
    {
        CB_VerificationType *top = &builder->frame->stack[builder->frame->stack_count - 1];
        if (!cb_type_is_reference(top))
        {
            fprintf(stderr, "codebuilder: type error at pc=%d in %s: astore expects reference, got %s\n",
                    codebuilder_current_pc(builder),
                    builder->method_name ? builder->method_name : "<unknown>",
                    cb_type_name(top));
        }
    }
    classfile_opcode_emit_astore(builder->method, index);
    CB_VerificationType value_type = cb_pop(builder);
    /* Set the local variable type from the stored value */
    codebuilder_set_local(builder, index, value_type);
}

void codebuilder_build_iastore(CodeBuilder *builder)
{
    classfile_opcode_emit_iastore(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_pop(builder);
}

void codebuilder_build_lastore(CodeBuilder *builder)
{
    classfile_opcode_emit_lastore(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_pop(builder);
}

void codebuilder_build_fastore(CodeBuilder *builder)
{
    classfile_opcode_emit_fastore(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_pop(builder);
}

void codebuilder_build_dastore(CodeBuilder *builder)
{
    classfile_opcode_emit_dastore(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_pop(builder);
}

void codebuilder_build_aastore(CodeBuilder *builder)
{
    classfile_opcode_emit_aastore(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_pop(builder);
}

void codebuilder_build_bastore(CodeBuilder *builder)
{
    classfile_opcode_emit_bastore(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_pop(builder);
}

void codebuilder_build_castore(CodeBuilder *builder)
{
    classfile_opcode_emit_castore(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_pop(builder);
}

void codebuilder_build_sastore(CodeBuilder *builder)
{
    classfile_opcode_emit_sastore(builder->method);
    cb_pop(builder);
    cb_pop(builder);
    cb_pop(builder);
}
