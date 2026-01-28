#pragma once

#include "codebuilder_defs.h"

void codebuilder_build_if(CodeBuilder *builder, IfCond cond, int offset);
void codebuilder_build_if_icmp(CodeBuilder *builder, IntCmpCond cond, int offset);
void codebuilder_build_if_acmp(CodeBuilder *builder, ACmpCond cond, int offset);
void codebuilder_build_goto(CodeBuilder *builder, int offset);
void codebuilder_build_ireturn(CodeBuilder *builder);
void codebuilder_build_lreturn(CodeBuilder *builder);
void codebuilder_build_freturn(CodeBuilder *builder);
void codebuilder_build_dreturn(CodeBuilder *builder);
void codebuilder_build_areturn(CodeBuilder *builder);
void codebuilder_build_return(CodeBuilder *builder);
void codebuilder_build_getstatic(CodeBuilder *builder, int index);
void codebuilder_build_putstatic(CodeBuilder *builder, int index);
void codebuilder_build_getfield(CodeBuilder *builder, int index);
void codebuilder_build_putfield(CodeBuilder *builder, int index);
void codebuilder_build_invokevirtual(CodeBuilder *builder, int index);
void codebuilder_build_invokespecial(CodeBuilder *builder, int index);
void codebuilder_build_invokestatic(CodeBuilder *builder, int index);
void codebuilder_build_new(CodeBuilder *builder, int class_index);
void codebuilder_build_newarray(CodeBuilder *builder, int atype);
void codebuilder_build_anewarray(CodeBuilder *builder, int class_index);
void codebuilder_build_arraylength(CodeBuilder *builder);
void codebuilder_build_athrow(CodeBuilder *builder);
void codebuilder_build_checkcast(CodeBuilder *builder, int class_index);
void codebuilder_build_instanceof(CodeBuilder *builder, int class_index);
void codebuilder_build_ifnull(CodeBuilder *builder, int offset);
void codebuilder_build_ifnonnull(CodeBuilder *builder, int offset);
