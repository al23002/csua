#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codegen_constants.h"
#include "codegen_symbols.h"
#include "codegen_jvm_types.h"
#include "codegenvisitor.h"
#include "codegenvisitor_expr_ops.h"
#include "codebuilder_ptr.h"
#include "codegenvisitor_util.h"
#include "codegenvisitor_expr_util.h"
#include "codebuilder_label.h"
#include "codebuilder_part1.h"
#include "codebuilder_part2.h"
#include "codebuilder_part3.h"
#include "cminor_type.h"
#include "constant_pool.h"
#include "code_output.h"

static void emit_ptr_add(CodegenVisitor *cg, Expression *expr);

/* Emit unsigned integer division: Integer.divideUnsigned(II)I */
static void emit_unsigned_idiv(CodegenVisitor *cg)
{
    int idx = cp_builder_add_methodref(code_output_cp(cg->output),
                                       "java/lang/Integer",
                                       "divideUnsigned",
                                       "(II)I");
    codebuilder_build_invokestatic(cg->builder, idx);
}

/* Emit unsigned long division: Long.divideUnsigned(JJ)J */
static void emit_unsigned_ldiv(CodegenVisitor *cg)
{
    int idx = cp_builder_add_methodref(code_output_cp(cg->output),
                                       "java/lang/Long",
                                       "divideUnsigned",
                                       "(JJ)J");
    codebuilder_build_invokestatic(cg->builder, idx);
}

/* Emit unsigned integer remainder: Integer.remainderUnsigned(II)I */
static void emit_unsigned_irem(CodegenVisitor *cg)
{
    int idx = cp_builder_add_methodref(code_output_cp(cg->output),
                                       "java/lang/Integer",
                                       "remainderUnsigned",
                                       "(II)I");
    codebuilder_build_invokestatic(cg->builder, idx);
}

/* Emit unsigned long remainder: Long.remainderUnsigned(JJ)J */
static void emit_unsigned_lrem(CodegenVisitor *cg)
{
    int idx = cp_builder_add_methodref(code_output_cp(cg->output),
                                       "java/lang/Long",
                                       "remainderUnsigned",
                                       "(JJ)J");
    codebuilder_build_invokestatic(cg->builder, idx);
}

/* Emit unsigned integer comparison: Integer.compareUnsigned(II)I
 * Returns negative if a < b, 0 if a == b, positive if a > b (unsigned) */
static void emit_unsigned_icmp(CodegenVisitor *cg)
{
    int idx = cp_builder_add_methodref(code_output_cp(cg->output),
                                       "java/lang/Integer",
                                       "compareUnsigned",
                                       "(II)I");
    codebuilder_build_invokestatic(cg->builder, idx);
}

/* Emit unsigned long comparison: Long.compareUnsigned(JJ)I
 * Returns negative if a < b, 0 if a == b, positive if a > b (unsigned) */
static void emit_unsigned_lcmp(CodegenVisitor *cg)
{
    int idx = cp_builder_add_methodref(code_output_cp(cg->output),
                                       "java/lang/Long",
                                       "compareUnsigned",
                                       "(JJ)I");
    codebuilder_build_invokestatic(cg->builder, idx);
}

static void emit_ptr_sub_int(CodegenVisitor *cg, Expression *expr);
static void emit_ptr_diff(CodegenVisitor *cg, Expression *expr);

/* Cast expression code generation for Java numeric type promotion */
void leave_castexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    CS_CastType ctype = expr->u.cast_expression.ctype;

    switch (ctype)
    {
    /* Widening conversions */
    case CS_CHAR_TO_INT:
        /* No JVM instruction needed - byte/char are already int on stack */
        break;
    case CS_SHORT_TO_INT:
        /* No JVM instruction needed - short is already int on stack */
        break;
    case CS_INT_TO_LONG:
        codebuilder_build_i2l(cg->builder);
        break;
    case CS_INT_TO_FLOAT:
        codebuilder_build_i2f(cg->builder);
        break;
    case CS_INT_TO_DOUBLE:
        codebuilder_build_i2d(cg->builder);
        break;
    case CS_LONG_TO_FLOAT:
        codebuilder_build_l2f(cg->builder);
        break;
    case CS_LONG_TO_DOUBLE:
        codebuilder_build_l2d(cg->builder);
        break;
    case CS_FLOAT_TO_DOUBLE:
        codebuilder_build_f2d(cg->builder);
        break;

    /* Unsigned widening: zero-extend instead of sign-extend */
    case CS_UCHAR_TO_INT:
        /* Zero-extend byte to int: mask with 0xFF */
        codebuilder_build_iconst(cg->builder, 255);
        codebuilder_build_iand(cg->builder);
        break;
    case CS_USHORT_TO_INT:
        /* Zero-extend short to int: mask with 0xFFFF */
        codebuilder_build_iconst(cg->builder, 65535);
        codebuilder_build_iand(cg->builder);
        break;
    case CS_UINT_TO_ULONG:
        /* i2l sign-extends, so we need to mask off the upper 32 bits */
        codebuilder_build_i2l(cg->builder);
        codebuilder_build_lconst(cg->builder, 0xFFFFFFFFL);
        codebuilder_build_land(cg->builder);
        break;

    /* Narrowing conversions */
    case CS_INT_TO_CHAR:
        codebuilder_build_i2b(cg->builder);
        break;
    case CS_INT_TO_SHORT:
        codebuilder_build_i2s(cg->builder);
        break;
    case CS_LONG_TO_INT:
        codebuilder_build_l2i(cg->builder);
        break;
    case CS_FLOAT_TO_INT:
        codebuilder_build_f2i(cg->builder);
        break;
    case CS_FLOAT_TO_LONG:
        codebuilder_build_f2l(cg->builder);
        break;
    case CS_DOUBLE_TO_INT:
        codebuilder_build_d2i(cg->builder);
        break;
    case CS_DOUBLE_TO_LONG:
        codebuilder_build_d2l(cg->builder);
        break;
    case CS_DOUBLE_TO_FLOAT:
        codebuilder_build_d2f(cg->builder);
        break;

    default:
        fprintf(stderr, "unknown cast type: %d\n", ctype);
        exit(1);
    }

    handle_for_expression_leave(cg, expr);
}

/* Explicit C-style type cast expression code generation: (type)expr
 * Emits appropriate JVM conversion instructions based on source/target types. */
void leave_typecastexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    Expression *operand = expr->u.type_cast_expression.expr;
    TypeSpecifier *target_type = expr->type;
    TypeSpecifier *src_type = operand ? operand->type : NULL;

    if (!src_type || !target_type)
    {
        handle_for_expression_leave(cg, expr);
        return;
    }

    /* Enum to int cast: no-op, enum is already int */
    if (cs_type_is_enum(src_type) && cs_type_is_int_exact(target_type))
    {
        handle_for_expression_leave(cg, expr);
        return;
    }

    /* Array to pointer cast: create pointer from array[0] */
    if (cs_type_is_array(src_type) && cs_type_is_pointer(target_type))
    {
        /* Stack: [array_ref] */
        codebuilder_build_iconst(cg->builder, 0);
        /* Stack: [array_ref, 0] */
        cg_emit_ptr_create(cg, target_type);
        /* Stack: [pointer] */
        handle_for_expression_leave(cg, expr);
        return;
    }

    /* No conversion needed if same type or both are non-numeric */
    if (!cs_type_is_numeric(src_type) || !cs_type_is_numeric(target_type) ||
        cs_type_same_basic(src_type, target_type))
    {
        handle_for_expression_leave(cg, expr);
        return;
    }

    /* Helpers for type categories */
    bool src_small = cs_type_is_char_exact(src_type) || cs_type_is_short_exact(src_type) ||
                     cs_type_is_int_exact(src_type);
    bool dst_small = cs_type_is_char_exact(target_type) || cs_type_is_short_exact(target_type) ||
                     cs_type_is_int_exact(target_type);

    /* char/short/int all treated as int on JVM stack */
    if (src_small && dst_small)
    {
        /* Narrowing to char/short may need truncation */
        if (cs_type_is_char_exact(target_type))
        {
            codebuilder_build_i2b(cg->builder);
        }
        else if (cs_type_is_short_exact(target_type))
        {
            codebuilder_build_i2s(cg->builder);
        }
        handle_for_expression_leave(cg, expr);
        return;
    }

    /* long conversions */
    if (cs_type_is_long_exact(src_type))
    {
        if (dst_small)
        {
            codebuilder_build_l2i(cg->builder);
            if (cs_type_is_char_exact(target_type))
                codebuilder_build_i2b(cg->builder);
            else if (cs_type_is_short_exact(target_type))
                codebuilder_build_i2s(cg->builder);
        }
        else if (cs_type_is_float_exact(target_type))
            codebuilder_build_l2f(cg->builder);
        else if (cs_type_is_double_exact(target_type))
            codebuilder_build_l2d(cg->builder);
    }
    else if (cs_type_is_long_exact(target_type))
    {
        if (src_small)
        {
            if (cs_type_is_unsigned(src_type))
            {
                /* unsigned int -> long: zero-extend */
                codebuilder_build_i2l(cg->builder);
                codebuilder_build_lconst(cg->builder, 0xFFFFFFFFL);
                codebuilder_build_land(cg->builder);
            }
            else
            {
                codebuilder_build_i2l(cg->builder);
            }
        }
        else if (cs_type_is_float_exact(src_type))
            codebuilder_build_f2l(cg->builder);
        else if (cs_type_is_double_exact(src_type))
            codebuilder_build_d2l(cg->builder);
    }
    /* float conversions */
    else if (cs_type_is_float_exact(src_type))
    {
        if (dst_small)
        {
            codebuilder_build_f2i(cg->builder);
            if (cs_type_is_char_exact(target_type))
                codebuilder_build_i2b(cg->builder);
            else if (cs_type_is_short_exact(target_type))
                codebuilder_build_i2s(cg->builder);
        }
        else if (cs_type_is_double_exact(target_type))
            codebuilder_build_f2d(cg->builder);
    }
    else if (cs_type_is_float_exact(target_type))
    {
        if (src_small)
            codebuilder_build_i2f(cg->builder);
        else if (cs_type_is_double_exact(src_type))
            codebuilder_build_d2f(cg->builder);
    }
    /* double conversions */
    else if (cs_type_is_double_exact(src_type))
    {
        if (dst_small)
        {
            codebuilder_build_d2i(cg->builder);
            if (cs_type_is_char_exact(target_type))
                codebuilder_build_i2b(cg->builder);
            else if (cs_type_is_short_exact(target_type))
                codebuilder_build_i2s(cg->builder);
        }
    }
    else if (cs_type_is_double_exact(target_type))
    {
        if (src_small)
            codebuilder_build_i2d(cg->builder);
    }

    handle_for_expression_leave(cg, expr);
}

/* Array to pointer implicit conversion.
 * Stack before: [array_ref]
 * Stack after: [pointer_object]
 * Creates a pointer to the first element of the array (index 0). */
void leave_array_to_pointer_expr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;

    /* Stack: [array_ref] from traversing the array expression */
    /* Push index 0 */
    codebuilder_build_iconst(cg->builder, 0);
    /* Stack: [array_ref, 0] */
    /* Create pointer object */
    cg_emit_ptr_create(cg, expr->type);
    /* Stack: [pointer] */

    handle_for_expression_leave(cg, expr);
}

/* Convert IfCond to IntCmpCond (same condition, different instruction type) */
static IntCmpCond if_cond_to_icmp_cond(IfCond cond)
{
    switch (cond)
    {
    case IF_EQ:
        return ICMP_EQ;
    case IF_NE:
        return ICMP_NE;
    case IF_LT:
        return ICMP_LT;
    case IF_GE:
        return ICMP_GE;
    case IF_GT:
        return ICMP_GT;
    case IF_LE:
        return ICMP_LE;
    }
    fprintf(stderr, "invalid IfCond for conversion: %d\n", cond);
    exit(1);
}

static void emit_ptr_add(CodegenVisitor *cg, Expression *expr)
{
    /* Stack: [ptr, int/long] or [int/long, ptr] */
    Expression *left = expr->u.binary_expression.left;
    Expression *right = expr->u.binary_expression.right;
    bool left_is_ptr = cs_type_is_pointer(left->type);
    TypeSpecifier *ptr_type = left_is_ptr ? left->type : right->type;
    TypeSpecifier *int_type = left_is_ptr ? right->type : left->type;

    if (!left_is_ptr)
    {
        /* int/long + ptr -> need to reorder for ptr + int */
        if (cs_type_is_long_exact(int_type))
        {
            /* Stack: [long, ptr] -> [int, ptr] -> [ptr, int] */
            codebuilder_build_l2i(cg->builder);
        }
        codebuilder_build_swap(cg->builder);
    }
    else if (cs_type_is_long_exact(int_type))
    {
        /* Stack: [ptr, long] -> [ptr, int] */
        codebuilder_build_l2i(cg->builder);
    }
    /* Stack: [ptr, int] */
    cg_emit_ptr_add(cg, ptr_type);
    handle_for_expression_leave(cg, expr);
}

static void emit_ptr_sub_int(CodegenVisitor *cg, Expression *expr)
{
    /* Stack: [ptr, int/long] -> ptr + (-int) */
    Expression *left = expr->u.binary_expression.left;
    Expression *right = expr->u.binary_expression.right;
    if (cs_type_is_long_exact(right->type))
    {
        /* Stack: [ptr, long] -> [ptr, int] */
        codebuilder_build_l2i(cg->builder);
    }
    codebuilder_build_ineg(cg->builder);
    cg_emit_ptr_add(cg, left->type);
    handle_for_expression_leave(cg, expr);
}

static void emit_ptr_diff(CodegenVisitor *cg, Expression *expr)
{
    /* Stack: [ptr1, ptr2] -> int */
    Expression *left = expr->u.binary_expression.left;
    cg_emit_ptr_diff(cg, left->type);
    handle_for_expression_leave(cg, expr);
}

void leave_addexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    Expression *left = expr->u.binary_expression.left;
    Expression *right = expr->u.binary_expression.right;

    /* 型だけでポインタ判定（デリファレンス結果がポインタの場合もある） */
    bool left_ptr = cs_type_is_pointer(left->type);
    bool right_ptr = cs_type_is_pointer(right->type);

    if (left_ptr || right_ptr)
    {
        /* ptr + int or int + ptr */
        emit_ptr_add(cg, expr);
    }
    else
    {
        TypeSpecifier *expr_type = expr->type;
        if (cs_type_is_double_exact(expr_type))
        {
            codebuilder_build_dadd(cg->builder);
        }
        else if (cs_type_is_float_exact(expr_type))
        {
            codebuilder_build_fadd(cg->builder);
        }
        else if (cs_type_is_long_exact(expr_type))
        {
            codebuilder_build_ladd(cg->builder);
        }
        else if (cs_type_is_int_exact(expr_type) || cs_type_is_short_exact(expr_type) || cs_type_is_char_exact(expr_type))
        {
            codebuilder_build_iadd(cg->builder);
        }
        else
        {
            fprintf(stderr, "unsupported numeric operand type in addition\n");
            exit(1);
        }
        handle_for_expression_leave(cg, expr);
    }
}

void leave_subexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    Expression *left = expr->u.binary_expression.left;
    Expression *right = expr->u.binary_expression.right;

    /* 型だけでポインタ判定（デリファレンス結果がポインタの場合もある） */
    bool left_ptr = cs_type_is_pointer(left->type);
    bool right_ptr = cs_type_is_pointer(right->type);

    if (left_ptr && right_ptr)
    {
        /* ptr - ptr */
        emit_ptr_diff(cg, expr);
    }
    else if (left_ptr && !right_ptr)
    {
        /* ptr - int */
        emit_ptr_sub_int(cg, expr);
    }
    else
    {
        TypeSpecifier *expr_type = expr->type;
        if (cs_type_is_double_exact(expr_type))
        {
            codebuilder_build_dsub(cg->builder);
        }
        else if (cs_type_is_float_exact(expr_type))
        {
            codebuilder_build_fsub(cg->builder);
        }
        else if (cs_type_is_long_exact(expr_type))
        {
            codebuilder_build_lsub(cg->builder);
        }
        else if (cs_type_is_int_exact(expr_type) || cs_type_is_short_exact(expr_type) || cs_type_is_char_exact(expr_type))
        {
            codebuilder_build_isub(cg->builder);
        }
        else
        {
            fprintf(stderr, "unsupported numeric operand type in subtraction\n");
            exit(1);
        }
        handle_for_expression_leave(cg, expr);
    }
}

void leave_mulexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    TypeSpecifier *expr_type = expr->type;
    if (cs_type_is_double_exact(expr_type))
    {
        codebuilder_build_dmul(cg->builder);
    }
    else if (cs_type_is_float_exact(expr_type))
    {
        codebuilder_build_fmul(cg->builder);
    }
    else if (cs_type_is_long_exact(expr_type))
    {
        codebuilder_build_lmul(cg->builder);
    }
    else if (cs_type_is_int_exact(expr_type) || cs_type_is_short_exact(expr_type) || cs_type_is_char_exact(expr_type))
    {
        codebuilder_build_imul(cg->builder);
    }
    else
    {
        fprintf(stderr, "unsupported numeric operand type in multiplication\n");
        exit(1);
    }
    handle_for_expression_leave(cg, expr);
}

void leave_divexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    TypeSpecifier *expr_type = expr->type;
    bool is_unsigned = cs_type_is_unsigned(expr_type);
    if (cs_type_is_double_exact(expr_type))
    {
        codebuilder_build_ddiv(cg->builder);
    }
    else if (cs_type_is_float_exact(expr_type))
    {
        codebuilder_build_fdiv(cg->builder);
    }
    else if (cs_type_is_long_exact(expr_type))
    {
        if (is_unsigned)
        {
            emit_unsigned_ldiv(cg);
        }
        else
        {
            codebuilder_build_ldiv(cg->builder);
        }
    }
    else if (cs_type_is_int_exact(expr_type) || cs_type_is_short_exact(expr_type) || cs_type_is_char_exact(expr_type))
    {
        if (is_unsigned)
        {
            emit_unsigned_idiv(cg);
        }
        else
        {
            codebuilder_build_idiv(cg->builder);
        }
    }
    else
    {
        fprintf(stderr, "unsupported numeric operand type in division\n");
        exit(1);
    }
    handle_for_expression_leave(cg, expr);
}

void leave_modexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    TypeSpecifier *expr_type = expr->type;
    bool is_unsigned = cs_type_is_unsigned(expr_type);
    if (cs_type_is_double_exact(expr_type))
    {
        codebuilder_build_drem(cg->builder);
    }
    else if (cs_type_is_float_exact(expr_type))
    {
        codebuilder_build_frem(cg->builder);
    }
    else if (cs_type_is_long_exact(expr_type))
    {
        if (is_unsigned)
        {
            emit_unsigned_lrem(cg);
        }
        else
        {
            codebuilder_build_lrem(cg->builder);
        }
    }
    else if (cs_type_is_int_exact(expr_type) || cs_type_is_short_exact(expr_type) || cs_type_is_char_exact(expr_type))
    {
        if (is_unsigned)
        {
            emit_unsigned_irem(cg);
        }
        else
        {
            codebuilder_build_irem(cg->builder);
        }
    }
    else
    {
        fprintf(stderr, "unsupported numeric operand type in modulo\n");
        exit(1);
    }
    handle_for_expression_leave(cg, expr);
}

void leave_bit_and_expr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    TypeSpecifier *expr_type = expr->type;
    if (cs_type_is_long_exact(expr_type))
    {
        codebuilder_build_land(cg->builder);
    }
    else if (cs_type_is_int_exact(expr_type) || cs_type_is_short_exact(expr_type) || cs_type_is_char_exact(expr_type))
    {
        codebuilder_build_iand(cg->builder);
    }
    else
    {
        fprintf(stderr, "unsupported bitwise operand type in bitwise AND\n");
        exit(1);
    }
    handle_for_expression_leave(cg, expr);
}

void leave_bit_or_expr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    TypeSpecifier *expr_type = expr->type;
    if (cs_type_is_long_exact(expr_type))
    {
        codebuilder_build_lor(cg->builder);
    }
    else if (cs_type_is_int_exact(expr_type) || cs_type_is_short_exact(expr_type) || cs_type_is_char_exact(expr_type))
    {
        codebuilder_build_ior(cg->builder);
    }
    else
    {
        fprintf(stderr, "unsupported bitwise operand type in bitwise OR\n");
        exit(1);
    }
    handle_for_expression_leave(cg, expr);
}

void leave_bit_xor_expr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    TypeSpecifier *expr_type = expr->type;
    if (cs_type_is_long_exact(expr_type))
    {
        codebuilder_build_lxor(cg->builder);
    }
    else if (cs_type_is_int_exact(expr_type) || cs_type_is_short_exact(expr_type) || cs_type_is_char_exact(expr_type))
    {
        codebuilder_build_ixor(cg->builder);
    }
    else
    {
        fprintf(stderr, "unsupported bitwise operand type in bitwise XOR\n");
        exit(1);
    }
    handle_for_expression_leave(cg, expr);
}

void leave_lshift_expr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    TypeSpecifier *expr_type = expr->type;
    if (cs_type_is_long_exact(expr_type))
    {
        codebuilder_build_lshl(cg->builder);
    }
    else if (cs_type_is_int_exact(expr_type) || cs_type_is_short_exact(expr_type) || cs_type_is_char_exact(expr_type))
    {
        codebuilder_build_ishl(cg->builder);
    }
    else
    {
        fprintf(stderr, "unsupported bitwise operand type in left shift\n");
        exit(1);
    }
    handle_for_expression_leave(cg, expr);
}

void leave_rshift_expr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    TypeSpecifier *expr_type = expr->type;
    bool is_unsigned = cs_type_is_unsigned(expr_type);
    if (cs_type_is_long_exact(expr_type))
    {
        if (is_unsigned)
        {
            codebuilder_build_lushr(cg->builder); /* unsigned: logical right shift */
        }
        else
        {
            codebuilder_build_lshr(cg->builder); /* signed: arithmetic right shift */
        }
    }
    else if (cs_type_is_int_exact(expr_type) || cs_type_is_short_exact(expr_type) || cs_type_is_char_exact(expr_type))
    {
        if (is_unsigned)
        {
            codebuilder_build_iushr(cg->builder); /* unsigned: logical right shift */
        }
        else
        {
            codebuilder_build_ishr(cg->builder); /* signed: arithmetic right shift */
        }
    }
    else
    {
        fprintf(stderr, "unsupported bitwise operand type in right shift\n");
        exit(1);
    }
    handle_for_expression_leave(cg, expr);
}

void leave_bit_not_expr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    TypeSpecifier *expr_type = expr->type;
    if (cs_type_is_long_exact(expr_type))
    {
        codebuilder_build_lconst(cg->builder, -1);
        codebuilder_build_lxor(cg->builder);
    }
    else if (cs_type_is_int_exact(expr_type) || cs_type_is_short_exact(expr_type) || cs_type_is_char_exact(expr_type))
    {
        codebuilder_build_iconst(cg->builder, -1);
        codebuilder_build_ixor(cg->builder);
    }
    else
    {
        fprintf(stderr, "bitwise NOT requires integer type\n");
        exit(1);
    }
    handle_for_expression_leave(cg, expr);
}

void leave_unary_minus_expr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    TypeSpecifier *expr_type = expr->type;
    if (cs_type_is_double_exact(expr_type))
    {
        codebuilder_build_dneg(cg->builder);
    }
    else if (cs_type_is_float_exact(expr_type))
    {
        codebuilder_build_fneg(cg->builder);
    }
    else if (cs_type_is_long_exact(expr_type))
    {
        codebuilder_build_lneg(cg->builder);
    }
    else if (cs_type_is_int_exact(expr_type) || cs_type_is_short_exact(expr_type) || cs_type_is_char_exact(expr_type))
    {
        codebuilder_build_ineg(cg->builder);
    }
    else
    {
        fprintf(stderr, "unsupported unary minus operand type\n");
        exit(1);
    }
    handle_for_expression_leave(cg, expr);
}

void leave_unary_plus_expr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    /* Unary plus: no operation needed, the value is already on the stack.
     * Type promotion was already handled by semantic analysis. */
    handle_for_expression_leave(cg, expr);
}

void leave_logical_not_expr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    TypeSpecifier *operand_type = expr->u.logical_not_expression->type;

    /* Logical NOT: value == 0/null -> 1, otherwise -> 0 */
    CB_Label *true_label = codebuilder_create_label(cg->builder);
    CB_Label *false_label = codebuilder_create_label(cg->builder);
    CB_Label *end_label = codebuilder_create_label(cg->builder);

    if (cs_type_is_pointer(operand_type))
    {
        if (cs_type_is_void_pointer(operand_type))
        {
            /* void* is raw Object reference */
            codebuilder_jump_if_null(cg->builder, true_label);
        }
        else
        {
            /* Pointer wrapper: check if .base field is null */
            cg_emit_ptr_get_base(cg, operand_type);
            codebuilder_jump_if_null(cg->builder, true_label);
        }
    }
    else
    {
        /* int/bool: ifeq -> true (1), else false (0) */
        codebuilder_jump_if_op(cg->builder, IF_EQ, true_label);
    }

    /* Fall-through: value was non-zero/non-null -> push 0 */
    codebuilder_place_label(cg->builder, false_label);
    codebuilder_build_iconst(cg->builder, 0);
    codebuilder_jump(cg->builder, end_label);

    /* True path: value was zero/null -> push 1 */
    codebuilder_place_label(cg->builder, true_label);
    codebuilder_build_iconst(cg->builder, 1);

    codebuilder_place_label(cg->builder, end_label);

    handle_for_expression_leave(cg, expr);
}

void leave_compareexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    Expression *left = expr->u.binary_expression.left;

    TypeSpecifier *left_type = left->type;
    IfCond cond = IF_EQ;

    switch (expr->kind)
    {
    case EQ_EXPRESSION:
        cond = IF_EQ;
        break;
    case NE_EXPRESSION:
        cond = IF_NE;
        break;
    case LT_EXPRESSION:
        cond = IF_LT;
        break;
    case LE_EXPRESSION:
        cond = IF_LE;
        break;
    case GT_EXPRESSION:
        cond = IF_GT;
        break;
    case GE_EXPRESSION:
        cond = IF_GE;
        break;
    default:
        fprintf(stderr, "unsupported comparison operator %d\n", expr->kind);
        exit(1);
    }

    if (cs_type_is_double_exact(left_type))
    {
        codebuilder_build_dcmp(cg->builder, cond == IF_LT || cond == IF_LE ? CMP_NAN_G : CMP_NAN_L);
        emit_if_comparison(cg, cond);
    }
    else if (cs_type_is_float_exact(left_type))
    {
        codebuilder_build_fcmp(cg->builder, cond == IF_LT || cond == IF_LE ? CMP_NAN_G : CMP_NAN_L);
        emit_if_comparison(cg, cond);
    }
    else if (cs_type_is_long_exact(left_type))
    {
        if (cs_type_is_unsigned(left_type))
        {
            /* Unsigned long comparison: use Long.compareUnsigned */
            emit_unsigned_lcmp(cg);
        }
        else
        {
            codebuilder_build_lcmp(cg->builder);
        }
        emit_if_comparison(cg, cond);
    }
    else if (cs_type_is_int_exact(left_type) || cs_type_is_short_exact(left_type) ||
             cs_type_is_char_exact(left_type) || cs_type_is_bool(left_type) ||
             cs_type_is_enum(left_type))
    {
        if (cs_type_is_unsigned(left_type))
        {
            /* Unsigned int comparison: use Integer.compareUnsigned
             * compareUnsigned returns int (-1, 0, 1), then compare with if_xx */
            emit_unsigned_icmp(cg);
            emit_if_comparison(cg, cond);
        }
        else
        {
            /* For signed int/enum comparisons, use emit_icmp_comparison which properly handles
             * StackMapTable frame recording for branch targets. */
            emit_icmp_comparison(cg, if_cond_to_icmp_cond(cond));
        }
    }
    else if (cs_type_is_pointer(left_type) || cs_type_is_pointer(expr->u.binary_expression.right->type))
    {
        /*
         * Pointer comparison: compare .base fields for NULL check.
         * Stack: [left_ptr, right_ptr]
         *
         * NULL literals should have been type-propagated to concrete pointer types
         * by meanvisitor. If void* remains here, it's an unsupported void* variable.
         */
        Expression *left = expr->u.binary_expression.left;
        Expression *right = expr->u.binary_expression.right;
        TypeSpecifier *right_type = right->type;

        /* Check for unsupported void* variables */
        if (cs_type_is_void_pointer(left_type) && left->kind != NULL_EXPRESSION)
        {
            fprintf(stderr, "void* comparison not supported\n");
            exit(1);
        }
        if (cs_type_is_void_pointer(right_type) && right->kind != NULL_EXPRESSION)
        {
            fprintf(stderr, "void* comparison not supported\n");
            exit(1);
        }

        /* Handle NULL literal comparisons */
        bool left_is_null = (left->kind == NULL_EXPRESSION);
        bool right_is_null = (right->kind == NULL_EXPRESSION);

        if (left_is_null && right_is_null)
        {
            /* NULL == NULL: always true for ==, false for != */
            codebuilder_build_pop(cg->builder);
            codebuilder_build_pop(cg->builder);
            codebuilder_build_iconst(cg->builder, (cond == IF_EQ) ? 1 : 0);
        }
        else if (left_is_null)
        {
            /* Check if right.base is null */
            codebuilder_build_swap(cg->builder);
            codebuilder_build_pop(cg->builder);
            cg_emit_ptr_get_base(cg, right_type);
            emit_if_ref_null_check(cg, cond == IF_EQ);
        }
        else if (right_is_null)
        {
            /* Check if left.base is null */
            codebuilder_build_pop(cg->builder);
            cg_emit_ptr_get_base(cg, left_type);
            emit_if_ref_null_check(cg, cond == IF_EQ);
        }
        else if (cond == IF_EQ || cond == IF_NE)
        {
            /* Equality comparison: compare .base AND .offset fields */
            /* Stack: [left_ptr, right_ptr] */
            int temp_right = allocate_temp_local_for_tag(cg, CF_VAL_OBJECT);
            int temp_left = allocate_temp_local_for_tag(cg, CF_VAL_OBJECT);

            codebuilder_build_astore(cg->builder, temp_right); /* [left_ptr] */
            codebuilder_build_astore(cg->builder, temp_left);  /* [] */

            /* Compare offsets first */
            codebuilder_build_aload(cg->builder, temp_left);  /* [left_ptr] */
            cg_emit_ptr_get_offset(cg, left_type);            /* [left_offset] */
            codebuilder_build_aload(cg->builder, temp_right); /* [left_offset, right_ptr] */
            cg_emit_ptr_get_offset(cg, right_type);           /* [left_offset, right_offset] */

            /* Create labels for branching */
            CB_Label *label_result_known = codebuilder_create_label(cg->builder);
            CB_Label *label_end = codebuilder_create_label(cg->builder);

            /* If offsets differ, jump to result_known */
            codebuilder_jump_if_icmp(cg->builder, ICMP_NE, label_result_known);

            /* Offsets are equal, now compare bases */
            codebuilder_build_aload(cg->builder, temp_left);  /* [left_ptr] */
            cg_emit_ptr_get_base(cg, left_type);              /* [left_base] */
            codebuilder_build_aload(cg->builder, temp_right); /* [left_base, right_ptr] */
            cg_emit_ptr_get_base(cg, right_type);             /* [left_base, right_base] */

            ACmpCond acond = (cond == IF_EQ) ? ACMP_EQ : ACMP_NE;
            emit_acmp_comparison(cg, acond);
            codebuilder_jump(cg->builder, label_end);

            /* label_result_known: offsets differed */
            codebuilder_place_label(cg->builder, label_result_known);
            if (cond == IF_EQ)
            {
                codebuilder_build_iconst(cg->builder, 0); /* false */
            }
            else
            {
                codebuilder_build_iconst(cg->builder, 1); /* true */
            }

            codebuilder_place_label(cg->builder, label_end);
        }
        else
        {
            /* Relational comparison (<, >, <=, >=): compare .offset fields only */
            /* Assumes both pointers point into the same array */
            /* Stack: [left_ptr, right_ptr] */
            int temp_right = allocate_temp_local_for_tag(cg, CF_VAL_OBJECT);

            codebuilder_build_astore(cg->builder, temp_right); /* [left_ptr] */
            cg_emit_ptr_get_offset(cg, left_type);             /* [left_offset] */
            codebuilder_build_aload(cg->builder, temp_right);  /* [left_offset, right_ptr] */
            cg_emit_ptr_get_offset(cg, right_type);            /* [left_offset, right_offset] */

            /* Compare offsets as integers */
            emit_icmp_comparison(cg, if_cond_to_icmp_cond(cond));
        }
    }
    else
    {
        fprintf(stderr, "unsupported compare operand type\n");
        exit(1);
    }

    handle_for_expression_leave(cg, expr);
}

void leave_conditionalexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;

    Expression *condition = expr->u.conditional_expression.condition;
    Expression *true_expr = expr->u.conditional_expression.true_expr;
    Expression *false_expr = expr->u.conditional_expression.false_expr;

    /* Create labels for control flow */
    CB_Label *false_label = codebuilder_create_label(cg->builder);
    CB_Label *end_label = codebuilder_create_label(cg->builder);

    /* Evaluate condition */
    codegen_traverse_expr(condition, cg);

    /* Jump to false_label based on condition type:
     * - bool/int: ifeq (if value == 0)
     * - pointer: check .base field is null
     * - array: ifnull (if reference == null) */
    TypeSpecifier *cond_type = condition->type;
    if (cs_type_is_pointer(cond_type))
    {
        if (cs_type_is_void_pointer(cond_type))
        {
            /* void* is raw Object reference */
            codebuilder_jump_if_null(cg->builder, false_label);
        }
        else
        {
            /* Pointer wrapper: check if .base field is null */
            cg_emit_ptr_get_base(cg, cond_type);
            codebuilder_jump_if_null(cg->builder, false_label);
        }
    }
    else if (cs_type_is_array(cond_type))
    {
        codebuilder_jump_if_null(cg->builder, false_label);
    }
    else
    {
        codebuilder_jump_if_op(cg->builder, IF_EQ, false_label);
    }

    /* True branch: evaluate true_expr */
    codegen_traverse_expr(true_expr, cg);

    /* Jump to end_label */
    codebuilder_jump(cg->builder, end_label);

    /* False branch */
    codebuilder_place_label(cg->builder, false_label);

    /* Evaluate false_expr */
    codegen_traverse_expr(false_expr, cg);

    /* End label */
    codebuilder_place_label(cg->builder, end_label);

    handle_for_expression_leave(cg, expr);
}

void leave_logical_and_expr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;

    Expression *left = expr->u.binary_expression.left;
    Expression *right = expr->u.binary_expression.right;

    /* Create labels for control flow */
    CB_Label *false_label = codebuilder_create_label(cg->builder);
    CB_Label *end_label = codebuilder_create_label(cg->builder);

    /* Evaluate left operand */
    codegen_traverse_expr(left, cg);

    /* If left is 0/null, short-circuit to false */
    TypeSpecifier *left_type = left->type;
    if (cs_type_is_pointer(left_type))
    {
        if (cs_type_is_void_pointer(left_type))
        {
            codebuilder_jump_if_null(cg->builder, false_label);
        }
        else
        {
            cg_emit_ptr_get_base(cg, left_type);
            codebuilder_jump_if_null(cg->builder, false_label);
        }
    }
    else if (cs_type_is_array(left_type))
    {
        codebuilder_jump_if_null(cg->builder, false_label);
    }
    else
    {
        codebuilder_jump_if_op(cg->builder, IF_EQ, false_label);
    }

    /* Evaluate right operand */
    codegen_traverse_expr(right, cg);

    /* If right is 0/null, short-circuit to false */
    TypeSpecifier *right_type = right->type;
    if (cs_type_is_pointer(right_type))
    {
        if (cs_type_is_void_pointer(right_type))
        {
            codebuilder_jump_if_null(cg->builder, false_label);
        }
        else
        {
            cg_emit_ptr_get_base(cg, right_type);
            codebuilder_jump_if_null(cg->builder, false_label);
        }
    }
    else if (cs_type_is_array(right_type))
    {
        codebuilder_jump_if_null(cg->builder, false_label);
    }
    else
    {
        codebuilder_jump_if_op(cg->builder, IF_EQ, false_label);
    }

    /* Both true, push 1 */
    codebuilder_build_iconst(cg->builder, 1);
    codebuilder_jump(cg->builder, end_label);

    /* False branch */
    codebuilder_place_label(cg->builder, false_label);
    codebuilder_build_iconst(cg->builder, 0);

    /* End label */
    codebuilder_place_label(cg->builder, end_label);

    handle_for_expression_leave(cg, expr);
}

void leave_logical_or_expr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;

    Expression *left = expr->u.binary_expression.left;
    Expression *right = expr->u.binary_expression.right;

    /* Create labels for control flow */
    CB_Label *true_label = codebuilder_create_label(cg->builder);
    CB_Label *end_label = codebuilder_create_label(cg->builder);

    /* Evaluate left operand */
    codegen_traverse_expr(left, cg);

    /* If left is non-zero/non-null, short-circuit to true */
    TypeSpecifier *left_type = left->type;
    if (cs_type_is_pointer(left_type))
    {
        if (cs_type_is_void_pointer(left_type))
        {
            codebuilder_jump_if_not_null(cg->builder, true_label);
        }
        else
        {
            cg_emit_ptr_get_base(cg, left_type);
            codebuilder_jump_if_not_null(cg->builder, true_label);
        }
    }
    else if (cs_type_is_array(left_type))
    {
        codebuilder_jump_if_not_null(cg->builder, true_label);
    }
    else
    {
        codebuilder_jump_if_op(cg->builder, IF_NE, true_label);
    }

    /* Evaluate right operand */
    codegen_traverse_expr(right, cg);

    /* If right is non-zero/non-null, short-circuit to true */
    TypeSpecifier *right_type = right->type;
    if (cs_type_is_pointer(right_type))
    {
        if (cs_type_is_void_pointer(right_type))
        {
            codebuilder_jump_if_not_null(cg->builder, true_label);
        }
        else
        {
            cg_emit_ptr_get_base(cg, right_type);
            codebuilder_jump_if_not_null(cg->builder, true_label);
        }
    }
    else if (cs_type_is_array(right_type))
    {
        codebuilder_jump_if_not_null(cg->builder, true_label);
    }
    else
    {
        codebuilder_jump_if_op(cg->builder, IF_NE, true_label);
    }

    /* Both false, push 0 */
    codebuilder_build_iconst(cg->builder, 0);
    codebuilder_jump(cg->builder, end_label);

    /* True branch */
    codebuilder_place_label(cg->builder, true_label);
    codebuilder_build_iconst(cg->builder, 1);

    /* End label */
    codebuilder_place_label(cg->builder, end_label);

    handle_for_expression_leave(cg, expr);
}
