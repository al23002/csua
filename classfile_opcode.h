#pragma once

#include "method_code.h"

/* Enums for opcode variants */

/* NaN handling for fcmp/dcmp: L returns -1 for NaN, G returns 1 for NaN */
typedef enum
{
    CMP_NAN_L,
    CMP_NAN_G
} CmpNaN;

/* Condition for if instructions (compare against zero) */
typedef enum
{
    IF_EQ,
    IF_NE,
    IF_LT,
    IF_GE,
    IF_GT,
    IF_LE
} IfCond;

/* Condition for if_icmp instructions (compare two integers) */
typedef enum
{
    ICMP_EQ,
    ICMP_NE,
    ICMP_LT,
    ICMP_GE,
    ICMP_GT,
    ICMP_LE
} IntCmpCond;

/* Condition for if_acmp instructions (compare two references) */
typedef enum
{
    ACMP_EQ,
    ACMP_NE
} ACmpCond;

void classfile_opcode_emit_nop(MethodCode *mc);
void classfile_opcode_emit_aconst_null(MethodCode *mc);
void classfile_opcode_emit_iconst(MethodCode *mc, int value);
void classfile_opcode_emit_lconst(MethodCode *mc, long value);
void classfile_opcode_emit_fconst(MethodCode *mc, float value);
void classfile_opcode_emit_dconst(MethodCode *mc, double value);
void classfile_opcode_emit_bipush(MethodCode *mc, int value);
void classfile_opcode_emit_sipush(MethodCode *mc, int value);
void classfile_opcode_emit_ldc(MethodCode *mc, int index);
void classfile_opcode_emit_ldc_w(MethodCode *mc, int index);
void classfile_opcode_emit_ldc2_w(MethodCode *mc, int index);
void classfile_opcode_emit_iload(MethodCode *mc, int index);
void classfile_opcode_emit_lload(MethodCode *mc, int index);
void classfile_opcode_emit_fload(MethodCode *mc, int index);
void classfile_opcode_emit_dload(MethodCode *mc, int index);
void classfile_opcode_emit_aload(MethodCode *mc, int index);
void classfile_opcode_emit_iload_n(MethodCode *mc, int index);
void classfile_opcode_emit_lload_n(MethodCode *mc, int index);
void classfile_opcode_emit_fload_n(MethodCode *mc, int index);
void classfile_opcode_emit_dload_n(MethodCode *mc, int index);
void classfile_opcode_emit_aload_n(MethodCode *mc, int index);
void classfile_opcode_emit_iaload(MethodCode *mc);
void classfile_opcode_emit_laload(MethodCode *mc);
void classfile_opcode_emit_faload(MethodCode *mc);
void classfile_opcode_emit_daload(MethodCode *mc);
void classfile_opcode_emit_aaload(MethodCode *mc);
void classfile_opcode_emit_baload(MethodCode *mc);
void classfile_opcode_emit_caload(MethodCode *mc);
void classfile_opcode_emit_saload(MethodCode *mc);
void classfile_opcode_emit_istore(MethodCode *mc, int index);
void classfile_opcode_emit_lstore(MethodCode *mc, int index);
void classfile_opcode_emit_fstore(MethodCode *mc, int index);
void classfile_opcode_emit_dstore(MethodCode *mc, int index);
void classfile_opcode_emit_astore(MethodCode *mc, int index);
void classfile_opcode_emit_istore_n(MethodCode *mc, int index);
void classfile_opcode_emit_lstore_n(MethodCode *mc, int index);
void classfile_opcode_emit_fstore_n(MethodCode *mc, int index);
void classfile_opcode_emit_dstore_n(MethodCode *mc, int index);
void classfile_opcode_emit_astore_n(MethodCode *mc, int index);
void classfile_opcode_emit_iastore(MethodCode *mc);
void classfile_opcode_emit_lastore(MethodCode *mc);
void classfile_opcode_emit_fastore(MethodCode *mc);
void classfile_opcode_emit_dastore(MethodCode *mc);
void classfile_opcode_emit_aastore(MethodCode *mc);
void classfile_opcode_emit_bastore(MethodCode *mc);
void classfile_opcode_emit_castore(MethodCode *mc);
void classfile_opcode_emit_sastore(MethodCode *mc);
void classfile_opcode_emit_pop(MethodCode *mc);
void classfile_opcode_emit_pop2(MethodCode *mc);
void classfile_opcode_emit_dup(MethodCode *mc);
void classfile_opcode_emit_dup_x1(MethodCode *mc);
void classfile_opcode_emit_dup_x2(MethodCode *mc);
void classfile_opcode_emit_dup2(MethodCode *mc);
void classfile_opcode_emit_dup2_x1(MethodCode *mc);
void classfile_opcode_emit_dup2_x2(MethodCode *mc);
void classfile_opcode_emit_swap(MethodCode *mc);
void classfile_opcode_emit_iadd(MethodCode *mc);
void classfile_opcode_emit_ladd(MethodCode *mc);
void classfile_opcode_emit_fadd(MethodCode *mc);
void classfile_opcode_emit_dadd(MethodCode *mc);
void classfile_opcode_emit_isub(MethodCode *mc);
void classfile_opcode_emit_lsub(MethodCode *mc);
void classfile_opcode_emit_fsub(MethodCode *mc);
void classfile_opcode_emit_dsub(MethodCode *mc);
void classfile_opcode_emit_imul(MethodCode *mc);
void classfile_opcode_emit_lmul(MethodCode *mc);
void classfile_opcode_emit_fmul(MethodCode *mc);
void classfile_opcode_emit_dmul(MethodCode *mc);
void classfile_opcode_emit_idiv(MethodCode *mc);
void classfile_opcode_emit_ldiv(MethodCode *mc);
void classfile_opcode_emit_fdiv(MethodCode *mc);
void classfile_opcode_emit_ddiv(MethodCode *mc);
void classfile_opcode_emit_irem(MethodCode *mc);
void classfile_opcode_emit_lrem(MethodCode *mc);
void classfile_opcode_emit_frem(MethodCode *mc);
void classfile_opcode_emit_drem(MethodCode *mc);
void classfile_opcode_emit_ineg(MethodCode *mc);
void classfile_opcode_emit_lneg(MethodCode *mc);
void classfile_opcode_emit_fneg(MethodCode *mc);
void classfile_opcode_emit_dneg(MethodCode *mc);
void classfile_opcode_emit_ishl(MethodCode *mc);
void classfile_opcode_emit_lshl(MethodCode *mc);
void classfile_opcode_emit_ishr(MethodCode *mc);
void classfile_opcode_emit_lshr(MethodCode *mc);
void classfile_opcode_emit_iushr(MethodCode *mc);
void classfile_opcode_emit_lushr(MethodCode *mc);
void classfile_opcode_emit_iand(MethodCode *mc);
void classfile_opcode_emit_land(MethodCode *mc);
void classfile_opcode_emit_ior(MethodCode *mc);
void classfile_opcode_emit_lor(MethodCode *mc);
void classfile_opcode_emit_ixor(MethodCode *mc);
void classfile_opcode_emit_lxor(MethodCode *mc);
void classfile_opcode_emit_iinc(MethodCode *mc, int local_index, int increment);
void classfile_opcode_emit_i2l(MethodCode *mc);
void classfile_opcode_emit_i2f(MethodCode *mc);
void classfile_opcode_emit_i2d(MethodCode *mc);
void classfile_opcode_emit_l2i(MethodCode *mc);
void classfile_opcode_emit_l2f(MethodCode *mc);
void classfile_opcode_emit_l2d(MethodCode *mc);
void classfile_opcode_emit_f2i(MethodCode *mc);
void classfile_opcode_emit_f2l(MethodCode *mc);
void classfile_opcode_emit_f2d(MethodCode *mc);
void classfile_opcode_emit_d2i(MethodCode *mc);
void classfile_opcode_emit_d2l(MethodCode *mc);
void classfile_opcode_emit_d2f(MethodCode *mc);
void classfile_opcode_emit_i2b(MethodCode *mc);
void classfile_opcode_emit_i2c(MethodCode *mc);
void classfile_opcode_emit_i2s(MethodCode *mc);
void classfile_opcode_emit_lcmp(MethodCode *mc);
void classfile_opcode_emit_fcmp(MethodCode *mc, CmpNaN nan_behavior);
void classfile_opcode_emit_dcmp(MethodCode *mc, CmpNaN nan_behavior);
void classfile_opcode_emit_if(MethodCode *mc, IfCond cond, int offset);
void classfile_opcode_emit_if_icmp(MethodCode *mc, IntCmpCond cond, int offset);
void classfile_opcode_emit_if_acmp(MethodCode *mc, ACmpCond cond, int offset);
void classfile_opcode_emit_goto(MethodCode *mc, int offset);
void classfile_opcode_emit_ireturn(MethodCode *mc);
void classfile_opcode_emit_lreturn(MethodCode *mc);
void classfile_opcode_emit_freturn(MethodCode *mc);
void classfile_opcode_emit_dreturn(MethodCode *mc);
void classfile_opcode_emit_areturn(MethodCode *mc);
void classfile_opcode_emit_return(MethodCode *mc);
void classfile_opcode_emit_getstatic(MethodCode *mc, int field_index);
void classfile_opcode_emit_putstatic(MethodCode *mc, int field_index);
void classfile_opcode_emit_getfield(MethodCode *mc, int field_index);
void classfile_opcode_emit_putfield(MethodCode *mc, int field_index);
void classfile_opcode_emit_invokevirtual(MethodCode *mc, int method_index);
void classfile_opcode_emit_invokespecial(MethodCode *mc, int method_index);
void classfile_opcode_emit_invokestatic(MethodCode *mc, int method_index);
void classfile_opcode_emit_new(MethodCode *mc, int class_index);
void classfile_opcode_emit_newarray(MethodCode *mc, int atype);
void classfile_opcode_emit_anewarray(MethodCode *mc, int class_index);
void classfile_opcode_emit_arraylength(MethodCode *mc);
void classfile_opcode_emit_athrow(MethodCode *mc);
void classfile_opcode_emit_checkcast(MethodCode *mc, int class_index);
void classfile_opcode_emit_instanceof(MethodCode *mc, int class_index);
void classfile_opcode_emit_ifnull(MethodCode *mc, int offset);
void classfile_opcode_emit_ifnonnull(MethodCode *mc, int offset);
void classfile_opcode_emit_goto_w(MethodCode *mc, int offset);

/*
 * Switch instructions
 *
 * tableswitch: For dense case values. O(1) lookup via jump table.
 *   - low/high define the range of case values
 *   - offsets array has (high - low + 1) entries
 *   - Each offset is relative to the tableswitch opcode position
 *
 * lookupswitch: For sparse case values. O(log n) lookup via binary search.
 *   - npairs is the number of match-offset pairs
 *   - keys must be sorted in ascending order
 *   - Each offset is relative to the lookupswitch opcode position
 */
void classfile_opcode_emit_tableswitch(MethodCode *mc, int default_offset,
                                       int low, int high,
                                       const int *offsets);
void classfile_opcode_emit_lookupswitch(MethodCode *mc, int default_offset,
                                        int npairs,
                                        const int *keys,
                                        const int *offsets);
