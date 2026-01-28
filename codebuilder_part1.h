#pragma once

#include "codebuilder_defs.h"

void codebuilder_build_nop(CodeBuilder *builder);
void codebuilder_build_aconst_null(CodeBuilder *builder);
void codebuilder_build_iconst(CodeBuilder *builder, int value);
void codebuilder_build_lconst(CodeBuilder *builder, long value);
void codebuilder_build_fconst(CodeBuilder *builder, float value);
void codebuilder_build_dconst(CodeBuilder *builder, double value);
void codebuilder_build_ldc(CodeBuilder *builder, int index, CF_ValueTag tag);
void codebuilder_build_ldc2_w(CodeBuilder *builder, int index, CF_ValueTag tag);
void codebuilder_build_iload(CodeBuilder *builder, int index);
void codebuilder_build_lload(CodeBuilder *builder, int index);
void codebuilder_build_fload(CodeBuilder *builder, int index);
void codebuilder_build_dload(CodeBuilder *builder, int index);
void codebuilder_build_aload(CodeBuilder *builder, int index);
void codebuilder_build_iaload(CodeBuilder *builder);
void codebuilder_build_laload(CodeBuilder *builder);
void codebuilder_build_faload(CodeBuilder *builder);
void codebuilder_build_daload(CodeBuilder *builder);
void codebuilder_build_aaload(CodeBuilder *builder);
void codebuilder_build_baload(CodeBuilder *builder);
void codebuilder_build_caload(CodeBuilder *builder);
void codebuilder_build_saload(CodeBuilder *builder);
void codebuilder_build_istore(CodeBuilder *builder, int index);
void codebuilder_build_lstore(CodeBuilder *builder, int index);
void codebuilder_build_fstore(CodeBuilder *builder, int index);
void codebuilder_build_dstore(CodeBuilder *builder, int index);
void codebuilder_build_astore(CodeBuilder *builder, int index);
void codebuilder_build_iastore(CodeBuilder *builder);
void codebuilder_build_lastore(CodeBuilder *builder);
void codebuilder_build_fastore(CodeBuilder *builder);
void codebuilder_build_dastore(CodeBuilder *builder);
void codebuilder_build_aastore(CodeBuilder *builder);
void codebuilder_build_bastore(CodeBuilder *builder);
void codebuilder_build_castore(CodeBuilder *builder);
void codebuilder_build_sastore(CodeBuilder *builder);
