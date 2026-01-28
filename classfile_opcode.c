#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "classfile.h"
#include "classfile_opcode.h"

static void emit_u1(MethodCode *mc, int value)
{
    method_code_emit_u1(mc, (uint8_t)value);
}

static void emit_s1(MethodCode *mc, int value)
{
    emit_u1(mc, value);
}

static void emit_u2(MethodCode *mc, int value)
{
    method_code_emit_u2(mc, (uint16_t)value);
}

static void emit_s2(MethodCode *mc, int value)
{
    emit_u2(mc, value);
}

static void emit_s4(MethodCode *mc, int value)
{
    method_code_emit_u4(mc, (uint32_t)value);
}

static void emit_opcode(MethodCode *mc, CF_Opcode op)
{
    emit_u1(mc, (uint8_t)op);
}

static void emit_simple(MethodCode *mc, CF_Opcode op)
{
    emit_opcode(mc, op);
}

static void emit_u1_op(MethodCode *mc, CF_Opcode op, int value)
{
    emit_opcode(mc, op);
    emit_u1(mc, value);
}

static void emit_u2_op(MethodCode *mc, CF_Opcode op, int value)
{
    emit_opcode(mc, op);
    emit_u2(mc, value);
}

static void emit_local_indexed(MethodCode *mc, CF_Opcode op, int index)
{
    if (index <= 255)
    {
        emit_opcode(mc, op);
        emit_u1(mc, (uint8_t)index);
    }
    else
    {
        emit_opcode(mc, CF_WIDE);
        emit_opcode(mc, op);
        emit_u2(mc, (uint16_t)index);
    }
}

static void emit_branch(MethodCode *mc, CF_Opcode op, int offset)
{
    if (offset < -32768 || offset > 32767)
    {
        if (op == CF_GOTO)
        {
            emit_opcode(mc, CF_GOTO_W);
            emit_s4(mc, offset);
            return;
        }
        fprintf(stderr, "branch offset %d out of range for opcode %d\n", offset, op);
        exit(1);
    }

    emit_opcode(mc, op);
    emit_s2(mc, (int16_t)offset);
}

static CF_Opcode if_cond_to_opcode(IfCond cond)
{
    switch (cond)
    {
    case IF_EQ:
        return CF_IFEQ;
    case IF_NE:
        return CF_IFNE;
    case IF_LT:
        return CF_IFLT;
    case IF_GE:
        return CF_IFGE;
    case IF_GT:
        return CF_IFGT;
    case IF_LE:
        return CF_IFLE;
    }
    fprintf(stderr, "invalid IfCond %d\n", cond);
    exit(1);
}

static CF_Opcode int_cmp_cond_to_opcode(IntCmpCond cond)
{
    switch (cond)
    {
    case ICMP_EQ:
        return CF_IF_ICMPEQ;
    case ICMP_NE:
        return CF_IF_ICMPNE;
    case ICMP_LT:
        return CF_IF_ICMPLT;
    case ICMP_GE:
        return CF_IF_ICMPGE;
    case ICMP_GT:
        return CF_IF_ICMPGT;
    case ICMP_LE:
        return CF_IF_ICMPLE;
    }
    fprintf(stderr, "invalid IntCmpCond %d\n", cond);
    exit(1);
}

static CF_Opcode acmp_cond_to_opcode(ACmpCond cond)
{
    switch (cond)
    {
    case ACMP_EQ:
        return CF_IF_ACMPEQ;
    case ACMP_NE:
        return CF_IF_ACMPNE;
    }
    fprintf(stderr, "invalid ACmpCond %d\n", cond);
    exit(1);
}

static void emit_load_n(MethodCode *mc, CF_Opcode op, int index,
                        CF_Opcode op0, CF_Opcode op1, CF_Opcode op2, CF_Opcode op3)
{
    CF_Opcode selected;
    switch (index)
    {
    case 0:
        selected = op0;
        break;
    case 1:
        selected = op1;
        break;
    case 2:
        selected = op2;
        break;
    case 3:
        selected = op3;
        break;
    default:
        fprintf(stderr, "invalid opcode %d index %u\n", op, index);
        exit(1);
    }
    emit_simple(mc, selected);
}

/* Public low-level opcode emitters ----------------------------------------- */

void classfile_opcode_emit_nop(MethodCode *mc)
{
    emit_simple(mc, CF_NOP);
}

void classfile_opcode_emit_aconst_null(MethodCode *mc)
{
    emit_simple(mc, CF_ACONST_NULL);
}

void classfile_opcode_emit_iconst(MethodCode *mc, int value)
{
    switch (value)
    {
    case -1:
        emit_simple(mc, CF_ICONST_M1);
        break;
    case 0:
        emit_simple(mc, CF_ICONST_0);
        break;
    case 1:
        emit_simple(mc, CF_ICONST_1);
        break;
    case 2:
        emit_simple(mc, CF_ICONST_2);
        break;
    case 3:
        emit_simple(mc, CF_ICONST_3);
        break;
    case 4:
        emit_simple(mc, CF_ICONST_4);
        break;
    case 5:
        emit_simple(mc, CF_ICONST_5);
        break;
    default:
        fprintf(stderr, "iconst out of range: %d\n", value);
        exit(1);
    }
}

void classfile_opcode_emit_lconst(MethodCode *mc, long value)
{
    switch ((int)value)
    {
    case 0:
        emit_simple(mc, CF_LCONST_0);
        break;
    case 1:
        emit_simple(mc, CF_LCONST_1);
        break;
    default:
        fprintf(stderr, "lconst out of range: %ld\n", (long)value);
        exit(1);
    }
}

void classfile_opcode_emit_fconst(MethodCode *mc, float value)
{
    if (value == 0.0f)
    {
        emit_simple(mc, CF_FCONST_0);
    }
    else if (value == 1.0f)
    {
        emit_simple(mc, CF_FCONST_1);
    }
    else if (value == 2.0f)
    {
        emit_simple(mc, CF_FCONST_2);
    }
    else
    {
        fprintf(stderr, "fconst out of range: %f\n", value);
        exit(1);
    }
}

void classfile_opcode_emit_dconst(MethodCode *mc, double value)
{
    if (value == 0.0)
    {
        emit_simple(mc, CF_DCONST_0);
    }
    else if (value == 1.0)
    {
        emit_simple(mc, CF_DCONST_1);
    }
    else
    {
        fprintf(stderr, "dconst out of range: %f\n", value);
        exit(1);
    }
}

void classfile_opcode_emit_bipush(MethodCode *mc, int value)
{
    emit_u1_op(mc, CF_BIPUSH, (uint8_t)value);
}

void classfile_opcode_emit_sipush(MethodCode *mc, int value)
{
    emit_u2_op(mc, CF_SIPUSH, (uint16_t)value);
}

void classfile_opcode_emit_ldc(MethodCode *mc, int index)
{
    if (index > 255)
    {
        fprintf(stderr, "ldc index out of range: %u (use ldc_w)\n", index);
        exit(1);
    }
    emit_u1_op(mc, CF_LDC, (uint8_t)index);
}

void classfile_opcode_emit_ldc_w(MethodCode *mc, int index)
{
    emit_u2_op(mc, CF_LDC_W, (uint16_t)index);
}

void classfile_opcode_emit_ldc2_w(MethodCode *mc, int index)
{
    emit_u2_op(mc, CF_LDC2_W, (uint16_t)index);
}

void classfile_opcode_emit_iload(MethodCode *mc, int index)
{
    emit_local_indexed(mc, CF_ILOAD, index);
}

void classfile_opcode_emit_lload(MethodCode *mc, int index)
{
    emit_local_indexed(mc, CF_LLOAD, index);
}

void classfile_opcode_emit_fload(MethodCode *mc, int index)
{
    emit_local_indexed(mc, CF_FLOAD, index);
}

void classfile_opcode_emit_dload(MethodCode *mc, int index)
{
    emit_local_indexed(mc, CF_DLOAD, index);
}

void classfile_opcode_emit_aload(MethodCode *mc, int index)
{
    emit_local_indexed(mc, CF_ALOAD, index);
}

void classfile_opcode_emit_iload_n(MethodCode *mc, int index)
{
    emit_load_n(mc, CF_ILOAD, index, CF_ILOAD_0, CF_ILOAD_1, CF_ILOAD_2, CF_ILOAD_3);
}

void classfile_opcode_emit_lload_n(MethodCode *mc, int index)
{
    emit_load_n(mc, CF_LLOAD, index, CF_LLOAD_0, CF_LLOAD_1, CF_LLOAD_2, CF_LLOAD_3);
}

void classfile_opcode_emit_fload_n(MethodCode *mc, int index)
{
    emit_load_n(mc, CF_FLOAD, index, CF_FLOAD_0, CF_FLOAD_1, CF_FLOAD_2, CF_FLOAD_3);
}

void classfile_opcode_emit_dload_n(MethodCode *mc, int index)
{
    emit_load_n(mc, CF_DLOAD, index, CF_DLOAD_0, CF_DLOAD_1, CF_DLOAD_2, CF_DLOAD_3);
}

void classfile_opcode_emit_aload_n(MethodCode *mc, int index)
{
    emit_load_n(mc, CF_ALOAD, index, CF_ALOAD_0, CF_ALOAD_1, CF_ALOAD_2, CF_ALOAD_3);
}

void classfile_opcode_emit_iaload(MethodCode *mc)
{
    emit_simple(mc, CF_IALOAD);
}

void classfile_opcode_emit_laload(MethodCode *mc)
{
    emit_simple(mc, CF_LALOAD);
}

void classfile_opcode_emit_faload(MethodCode *mc)
{
    emit_simple(mc, CF_FALOAD);
}

void classfile_opcode_emit_daload(MethodCode *mc)
{
    emit_simple(mc, CF_DALOAD);
}

void classfile_opcode_emit_aaload(MethodCode *mc)
{
    emit_simple(mc, CF_AALOAD);
}

void classfile_opcode_emit_baload(MethodCode *mc)
{
    emit_simple(mc, CF_BALOAD);
}

void classfile_opcode_emit_caload(MethodCode *mc)
{
    emit_simple(mc, CF_CALOAD);
}

void classfile_opcode_emit_saload(MethodCode *mc)
{
    emit_simple(mc, CF_SALOAD);
}

void classfile_opcode_emit_istore(MethodCode *mc, int index)
{
    emit_local_indexed(mc, CF_ISTORE, index);
}

void classfile_opcode_emit_lstore(MethodCode *mc, int index)
{
    emit_local_indexed(mc, CF_LSTORE, index);
}

void classfile_opcode_emit_fstore(MethodCode *mc, int index)
{
    emit_local_indexed(mc, CF_FSTORE, index);
}

void classfile_opcode_emit_dstore(MethodCode *mc, int index)
{
    emit_local_indexed(mc, CF_DSTORE, index);
}

void classfile_opcode_emit_astore(MethodCode *mc, int index)
{
    emit_local_indexed(mc, CF_ASTORE, index);
}

void classfile_opcode_emit_istore_n(MethodCode *mc, int index)
{
    emit_load_n(mc, CF_ISTORE, index, CF_ISTORE_0, CF_ISTORE_1, CF_ISTORE_2, CF_ISTORE_3);
}

void classfile_opcode_emit_lstore_n(MethodCode *mc, int index)
{
    emit_load_n(mc, CF_LSTORE, index, CF_LSTORE_0, CF_LSTORE_1, CF_LSTORE_2, CF_LSTORE_3);
}

void classfile_opcode_emit_fstore_n(MethodCode *mc, int index)
{
    emit_load_n(mc, CF_FSTORE, index, CF_FSTORE_0, CF_FSTORE_1, CF_FSTORE_2, CF_FSTORE_3);
}

void classfile_opcode_emit_dstore_n(MethodCode *mc, int index)
{
    emit_load_n(mc, CF_DSTORE, index, CF_DSTORE_0, CF_DSTORE_1, CF_DSTORE_2, CF_DSTORE_3);
}

void classfile_opcode_emit_astore_n(MethodCode *mc, int index)
{
    emit_load_n(mc, CF_ASTORE, index, CF_ASTORE_0, CF_ASTORE_1, CF_ASTORE_2, CF_ASTORE_3);
}

void classfile_opcode_emit_iastore(MethodCode *mc)
{
    emit_simple(mc, CF_IASTORE);
}

void classfile_opcode_emit_lastore(MethodCode *mc)
{
    emit_simple(mc, CF_LASTORE);
}

void classfile_opcode_emit_fastore(MethodCode *mc)
{
    emit_simple(mc, CF_FASTORE);
}

void classfile_opcode_emit_dastore(MethodCode *mc)
{
    emit_simple(mc, CF_DASTORE);
}

void classfile_opcode_emit_aastore(MethodCode *mc)
{
    emit_simple(mc, CF_AASTORE);
}

void classfile_opcode_emit_bastore(MethodCode *mc)
{
    emit_simple(mc, CF_BASTORE);
}

void classfile_opcode_emit_castore(MethodCode *mc)
{
    emit_simple(mc, CF_CASTORE);
}

void classfile_opcode_emit_sastore(MethodCode *mc)
{
    emit_simple(mc, CF_SASTORE);
}

void classfile_opcode_emit_pop(MethodCode *mc)
{
    emit_simple(mc, CF_POP);
}

void classfile_opcode_emit_pop2(MethodCode *mc)
{
    emit_simple(mc, CF_POP2);
}

void classfile_opcode_emit_dup(MethodCode *mc)
{
    emit_simple(mc, CF_DUP);
}

void classfile_opcode_emit_dup_x1(MethodCode *mc)
{
    emit_simple(mc, CF_DUP_X1);
}

void classfile_opcode_emit_dup_x2(MethodCode *mc)
{
    emit_simple(mc, CF_DUP_X2);
}

void classfile_opcode_emit_dup2(MethodCode *mc)
{
    emit_simple(mc, CF_DUP2);
}

void classfile_opcode_emit_dup2_x1(MethodCode *mc)
{
    emit_simple(mc, CF_DUP2_X1);
}

void classfile_opcode_emit_dup2_x2(MethodCode *mc)
{
    emit_simple(mc, CF_DUP2_X2);
}

void classfile_opcode_emit_swap(MethodCode *mc)
{
    emit_simple(mc, CF_SWAP);
}

void classfile_opcode_emit_iadd(MethodCode *mc)
{
    emit_simple(mc, CF_IADD);
}

void classfile_opcode_emit_ladd(MethodCode *mc)
{
    emit_simple(mc, CF_LADD);
}

void classfile_opcode_emit_fadd(MethodCode *mc)
{
    emit_simple(mc, CF_FADD);
}

void classfile_opcode_emit_dadd(MethodCode *mc)
{
    emit_simple(mc, CF_DADD);
}

void classfile_opcode_emit_isub(MethodCode *mc)
{
    emit_simple(mc, CF_ISUB);
}

void classfile_opcode_emit_lsub(MethodCode *mc)
{
    emit_simple(mc, CF_LSUB);
}

void classfile_opcode_emit_fsub(MethodCode *mc)
{
    emit_simple(mc, CF_FSUB);
}

void classfile_opcode_emit_dsub(MethodCode *mc)
{
    emit_simple(mc, CF_DSUB);
}

void classfile_opcode_emit_imul(MethodCode *mc)
{
    emit_simple(mc, CF_IMUL);
}

void classfile_opcode_emit_lmul(MethodCode *mc)
{
    emit_simple(mc, CF_LMUL);
}

void classfile_opcode_emit_fmul(MethodCode *mc)
{
    emit_simple(mc, CF_FMUL);
}

void classfile_opcode_emit_dmul(MethodCode *mc)
{
    emit_simple(mc, CF_DMUL);
}

void classfile_opcode_emit_idiv(MethodCode *mc)
{
    emit_simple(mc, CF_IDIV);
}

void classfile_opcode_emit_ldiv(MethodCode *mc)
{
    emit_simple(mc, CF_LDIV);
}

void classfile_opcode_emit_fdiv(MethodCode *mc)
{
    emit_simple(mc, CF_FDIV);
}

void classfile_opcode_emit_ddiv(MethodCode *mc)
{
    emit_simple(mc, CF_DDIV);
}

void classfile_opcode_emit_irem(MethodCode *mc)
{
    emit_simple(mc, CF_IREM);
}

void classfile_opcode_emit_lrem(MethodCode *mc)
{
    emit_simple(mc, CF_LREM);
}

void classfile_opcode_emit_frem(MethodCode *mc)
{
    emit_simple(mc, CF_FREM);
}

void classfile_opcode_emit_drem(MethodCode *mc)
{
    emit_simple(mc, CF_DREM);
}

void classfile_opcode_emit_ineg(MethodCode *mc)
{
    emit_simple(mc, CF_INEG);
}

void classfile_opcode_emit_lneg(MethodCode *mc)
{
    emit_simple(mc, CF_LNEG);
}

void classfile_opcode_emit_fneg(MethodCode *mc)
{
    emit_simple(mc, CF_FNEG);
}

void classfile_opcode_emit_dneg(MethodCode *mc)
{
    emit_simple(mc, CF_DNEG);
}

void classfile_opcode_emit_ishl(MethodCode *mc)
{
    emit_simple(mc, CF_ISHL);
}

void classfile_opcode_emit_lshl(MethodCode *mc)
{
    emit_simple(mc, CF_LSHL);
}

void classfile_opcode_emit_ishr(MethodCode *mc)
{
    emit_simple(mc, CF_ISHR);
}

void classfile_opcode_emit_lshr(MethodCode *mc)
{
    emit_simple(mc, CF_LSHR);
}

void classfile_opcode_emit_iushr(MethodCode *mc)
{
    emit_simple(mc, CF_IUSHR);
}

void classfile_opcode_emit_lushr(MethodCode *mc)
{
    emit_simple(mc, CF_LUSHR);
}

void classfile_opcode_emit_iand(MethodCode *mc)
{
    emit_simple(mc, CF_IAND);
}

void classfile_opcode_emit_land(MethodCode *mc)
{
    emit_simple(mc, CF_LAND);
}

void classfile_opcode_emit_ior(MethodCode *mc)
{
    emit_simple(mc, CF_IOR);
}

void classfile_opcode_emit_lor(MethodCode *mc)
{
    emit_simple(mc, CF_LOR);
}

void classfile_opcode_emit_ixor(MethodCode *mc)
{
    emit_simple(mc, CF_IXOR);
}

void classfile_opcode_emit_lxor(MethodCode *mc)
{
    emit_simple(mc, CF_LXOR);
}

void classfile_opcode_emit_iinc(MethodCode *mc, int local_index, int increment)
{
    if (local_index <= 255)
    {
        emit_opcode(mc, CF_IINC);
        emit_u1(mc, (uint8_t)local_index);
        emit_s1(mc, increment);
    }
    else
    {
        emit_opcode(mc, CF_WIDE);
        emit_opcode(mc, CF_IINC);
        emit_u2(mc, (uint16_t)local_index);
        emit_s2(mc, (int16_t)increment);
    }
}

void classfile_opcode_emit_i2l(MethodCode *mc)
{
    emit_simple(mc, CF_I2L);
}

void classfile_opcode_emit_i2f(MethodCode *mc)
{
    emit_simple(mc, CF_I2F);
}

void classfile_opcode_emit_i2d(MethodCode *mc)
{
    emit_simple(mc, CF_I2D);
}

void classfile_opcode_emit_l2i(MethodCode *mc)
{
    emit_simple(mc, CF_L2I);
}

void classfile_opcode_emit_l2f(MethodCode *mc)
{
    emit_simple(mc, CF_L2F);
}

void classfile_opcode_emit_l2d(MethodCode *mc)
{
    emit_simple(mc, CF_L2D);
}

void classfile_opcode_emit_f2i(MethodCode *mc)
{
    emit_simple(mc, CF_F2I);
}

void classfile_opcode_emit_f2l(MethodCode *mc)
{
    emit_simple(mc, CF_F2L);
}

void classfile_opcode_emit_f2d(MethodCode *mc)
{
    emit_simple(mc, CF_F2D);
}

void classfile_opcode_emit_d2i(MethodCode *mc)
{
    emit_simple(mc, CF_D2I);
}

void classfile_opcode_emit_d2l(MethodCode *mc)
{
    emit_simple(mc, CF_D2L);
}

void classfile_opcode_emit_d2f(MethodCode *mc)
{
    emit_simple(mc, CF_D2F);
}

void classfile_opcode_emit_i2b(MethodCode *mc)
{
    emit_simple(mc, CF_I2B);
}

void classfile_opcode_emit_i2c(MethodCode *mc)
{
    emit_simple(mc, CF_I2C);
}

void classfile_opcode_emit_i2s(MethodCode *mc)
{
    emit_simple(mc, CF_I2S);
}

void classfile_opcode_emit_lcmp(MethodCode *mc)
{
    emit_simple(mc, CF_LCMP);
}

void classfile_opcode_emit_fcmp(MethodCode *mc, CmpNaN nan_behavior)
{
    CF_Opcode op = (nan_behavior == CMP_NAN_L) ? CF_FCMPL : CF_FCMPG;
    emit_simple(mc, op);
}

void classfile_opcode_emit_dcmp(MethodCode *mc, CmpNaN nan_behavior)
{
    CF_Opcode op = (nan_behavior == CMP_NAN_L) ? CF_DCMPL : CF_DCMPG;
    emit_simple(mc, op);
}

void classfile_opcode_emit_if(MethodCode *mc, IfCond cond, int offset)
{
    CF_Opcode op = if_cond_to_opcode(cond);
    emit_branch(mc, op, offset);
}

void classfile_opcode_emit_if_icmp(MethodCode *mc, IntCmpCond cond, int offset)
{
    CF_Opcode op = int_cmp_cond_to_opcode(cond);
    emit_branch(mc, op, offset);
}

void classfile_opcode_emit_if_acmp(MethodCode *mc, ACmpCond cond, int offset)
{
    CF_Opcode op = acmp_cond_to_opcode(cond);
    emit_branch(mc, op, offset);
}

void classfile_opcode_emit_goto(MethodCode *mc, int offset)
{
    emit_branch(mc, CF_GOTO, offset);
}

void classfile_opcode_emit_ireturn(MethodCode *mc)
{
    emit_simple(mc, CF_IRETURN);
}

void classfile_opcode_emit_lreturn(MethodCode *mc)
{
    emit_simple(mc, CF_LRETURN);
}

void classfile_opcode_emit_freturn(MethodCode *mc)
{
    emit_simple(mc, CF_FRETURN);
}

void classfile_opcode_emit_dreturn(MethodCode *mc)
{
    emit_simple(mc, CF_DRETURN);
}

void classfile_opcode_emit_areturn(MethodCode *mc)
{
    emit_simple(mc, CF_ARETURN);
}

void classfile_opcode_emit_return(MethodCode *mc)
{
    emit_simple(mc, CF_RETURN);
}

void classfile_opcode_emit_getstatic(MethodCode *mc, int field_index)
{
    emit_u2_op(mc, CF_GETSTATIC, (uint16_t)field_index);
}

void classfile_opcode_emit_putstatic(MethodCode *mc, int field_index)
{
    emit_u2_op(mc, CF_PUTSTATIC, (uint16_t)field_index);
}

void classfile_opcode_emit_getfield(MethodCode *mc, int field_index)
{
    emit_u2_op(mc, CF_GETFIELD, (uint16_t)field_index);
}

void classfile_opcode_emit_putfield(MethodCode *mc, int field_index)
{
    emit_u2_op(mc, CF_PUTFIELD, (uint16_t)field_index);
}

void classfile_opcode_emit_invokevirtual(MethodCode *mc, int method_index)
{
    emit_u2_op(mc, CF_INVOKEVIRTUAL, (uint16_t)method_index);
}

void classfile_opcode_emit_invokespecial(MethodCode *mc, int method_index)
{
    emit_u2_op(mc, CF_INVOKESPECIAL, (uint16_t)method_index);
}

void classfile_opcode_emit_invokestatic(MethodCode *mc, int method_index)
{
    emit_u2_op(mc, CF_INVOKESTATIC, (uint16_t)method_index);
}

void classfile_opcode_emit_new(MethodCode *mc, int class_index)
{
    emit_u2_op(mc, CF_NEW, (uint16_t)class_index);
}

void classfile_opcode_emit_newarray(MethodCode *mc, int atype)
{
    emit_u1_op(mc, CF_NEWARRAY, atype);
}

void classfile_opcode_emit_anewarray(MethodCode *mc, int class_index)
{
    emit_u2_op(mc, CF_ANEWARRAY, (uint16_t)class_index);
}

void classfile_opcode_emit_arraylength(MethodCode *mc)
{
    emit_simple(mc, CF_ARRAYLENGTH);
}

void classfile_opcode_emit_athrow(MethodCode *mc)
{
    emit_simple(mc, CF_ATHROW);
}

void classfile_opcode_emit_checkcast(MethodCode *mc, int class_index)
{
    emit_u2_op(mc, CF_CHECKCAST, (uint16_t)class_index);
}

void classfile_opcode_emit_instanceof(MethodCode *mc, int class_index)
{
    emit_u2_op(mc, CF_INSTANCEOF, (uint16_t)class_index);
}

void classfile_opcode_emit_ifnull(MethodCode *mc, int offset)
{
    emit_branch(mc, CF_IFNULL, offset);
}

void classfile_opcode_emit_ifnonnull(MethodCode *mc, int offset)
{
    emit_branch(mc, CF_IFNONNULL, offset);
}

void classfile_opcode_emit_goto_w(MethodCode *mc, int offset)
{
    emit_opcode(mc, CF_GOTO_W);
    emit_s4(mc, offset);
}

void classfile_opcode_emit_tableswitch(MethodCode *mc, int default_offset,
                                       int low, int high,
                                       const int *offsets)
{
    int opcode_pc = method_code_size(mc);
    emit_opcode(mc, CF_TABLESWITCH);

    /* Padding: align to 4-byte boundary relative to method start */
    /* After opcode, we need (opcode_pc + 1) to be aligned to 4 */
    int padding = (4 - ((opcode_pc + 1) % 4)) % 4;
    for (int i = 0; i < padding; i++)
    {
        emit_u1(mc, 0);
    }

    emit_s4(mc, default_offset);
    emit_s4(mc, low);
    emit_s4(mc, high);

    int table_size = high - low + 1;
    for (int i = 0; i < table_size; i++)
    {
        emit_s4(mc, offsets[i]);
    }
}

void classfile_opcode_emit_lookupswitch(MethodCode *mc, int default_offset,
                                        int npairs,
                                        const int *keys,
                                        const int *offsets)
{
    int opcode_pc = method_code_size(mc);
    emit_opcode(mc, CF_LOOKUPSWITCH);

    /* Padding: align to 4-byte boundary relative to method start */
    int padding = (4 - ((opcode_pc + 1) % 4)) % 4;
    for (int i = 0; i < padding; i++)
    {
        emit_u1(mc, 0);
    }

    emit_s4(mc, default_offset);
    emit_s4(mc, npairs);

    for (int32_t i = 0; i < npairs; i++)
    {
        emit_s4(mc, keys[i]);
        emit_s4(mc, offsets[i]);
    }
}
