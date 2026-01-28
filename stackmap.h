/*
 * StackMapTable Generation Infrastructure
 *
 * This header now only contains BytecodeInstr for CFG analysis.
 * StackMapTable generation has been moved to codebuilder_stackmap.c
 * which uses CodeBuilder's direct type tracking.
 */

#pragma once

#include "classfile.h"

/*
 * Bytecode Instruction - captures PC, length, and opcode for CFG analysis
 */
typedef struct BytecodeInstr_tag
{
    int pc;
    int length;
    CF_Opcode opcode;
} BytecodeInstr;
