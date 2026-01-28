#pragma once

#include <stdint.h>

#include "classfile.h"
#include "stackmap.h"

typedef struct CFG_Info_tag
{
    bool *is_block_start;
    bool *is_branch_target;
    bool *is_handler_entry;
    uint8_t *succ_count;
    int *succ_pc0;
    int *succ_pc1;
} CFG_Info;

CFG_Info cfg_build(const BytecodeInstr *instrs, int instr_count,
                   const uint8_t *code, int code_size,
                   const CF_ExceptionEntry *exceptions,
                   int exception_count);

void cfg_free(CFG_Info *cfg);
