#include "cfg.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static bool cfg_is_return_or_throw(CF_Opcode op)
{
    switch (op)
    {
    case CF_IRETURN:
    case CF_LRETURN:
    case CF_FRETURN:
    case CF_DRETURN:
    case CF_ARETURN:
    case CF_RETURN:
    case CF_ATHROW:
        return true;
    default:
        return false;
    }
}

static bool cfg_is_unconditional_branch(CF_Opcode op)
{
    return op == CF_GOTO || op == CF_GOTO_W;
}

static bool cfg_is_conditional_branch(CF_Opcode op)
{
    switch (op)
    {
    case CF_IF_ACMPEQ:
    case CF_IF_ACMPNE:
    case CF_IF_ICMPEQ:
    case CF_IF_ICMPNE:
    case CF_IF_ICMPLT:
    case CF_IF_ICMPGE:
    case CF_IF_ICMPGT:
    case CF_IF_ICMPLE:
    case CF_IFEQ:
    case CF_IFNE:
    case CF_IFLT:
    case CF_IFGE:
    case CF_IFGT:
    case CF_IFLE:
    case CF_IFNULL:
    case CF_IFNONNULL:
        return true;
    default:
        return false;
    }
}

static bool cfg_is_control_transfer(CF_Opcode op)
{
    return cfg_is_return_or_throw(op) || cfg_is_unconditional_branch(op) ||
           cfg_is_conditional_branch(op);
}

static bool cfg_read_s2(const uint8_t *code, int pos, int size, int16_t *out)
{
    if (!code || !out)
    {
        return false;
    }
    if (pos + 1 >= size)
    {
        return false;
    }
    *out = (int16_t)((code[pos] << 8) | code[pos + 1]);
    return true;
}

static bool cfg_read_s4(const uint8_t *code, int pos, int size, int32_t *out)
{
    if (!code || !out)
    {
        return false;
    }
    if (pos + 3 >= size)
    {
        return false;
    }
    *out = (int32_t)((code[pos] << 24) | (code[pos + 1] << 16) |
                     (code[pos + 2] << 8) | code[pos + 3]);
    return true;
}

static bool cfg_decode_branch_target(const BytecodeInstr *instr,
                                     const uint8_t *code, int code_size,
                                     int *target_pc)
{
    if (!instr || !code || !target_pc)
    {
        return false;
    }

    int pos = instr->pc + 1;
    if (instr->opcode == CF_GOTO_W)
    {
        int32_t offset = 0;
        if (!cfg_read_s4(code, pos, code_size, &offset))
        {
            return false;
        }
        int target = instr->pc + offset;
        if (target < 0)
        {
            return false;
        }
        *target_pc = target;
        return true;
    }

    int16_t offset = 0;
    if (!cfg_read_s2(code, pos, code_size, &offset))
    {
        return false;
    }
    int target = instr->pc + (int)offset;
    if (target < 0)
    {
        return false;
    }
    *target_pc = target;
    return true;
}

static int cfg_find_instr_index(const BytecodeInstr *instrs, int count,
                                int pc)
{
    int lo = 0;
    int hi = count - 1;
    while (lo <= hi)
    {
        int mid = lo + (hi - lo) / 2;
        int mid_pc = instrs[mid].pc;
        if (mid_pc == pc)
        {
            return mid;
        }
        if (mid_pc < pc)
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid - 1;
        }
    }
    return -1;
}

CFG_Info cfg_build(const BytecodeInstr *instrs, int instr_count,
                   const uint8_t *code, int code_size,
                   const CF_ExceptionEntry *exceptions,
                   int exception_count)
{
    CFG_Info cfg;
    cfg.is_block_start = NULL;
    cfg.is_branch_target = NULL;
    cfg.is_handler_entry = NULL;
    cfg.succ_count = NULL;
    cfg.succ_pc0 = NULL;
    cfg.succ_pc1 = NULL;

    if (!instrs || instr_count == 0)
    {
        return cfg;
    }

    cfg.is_block_start = (bool *)calloc(instr_count, sizeof(bool));
    cfg.is_branch_target = (bool *)calloc(instr_count, sizeof(bool));
    cfg.is_handler_entry = (bool *)calloc(instr_count, sizeof(bool));
    cfg.succ_count = (uint8_t *)calloc(instr_count, sizeof(uint8_t));
    cfg.succ_pc0 = (int *)calloc(instr_count, sizeof(int));
    cfg.succ_pc1 = (int *)calloc(instr_count, sizeof(int));

    cfg.is_block_start[0] = true;

    for (int i = 0; i < instr_count; ++i)
    {
        const BytecodeInstr *instr = &instrs[i];
        int succ_count = 0;
        if (cfg_is_conditional_branch(instr->opcode))
        {
            int target = 0;
            if (cfg_decode_branch_target(instr, code, code_size, &target))
            {
                cfg.succ_pc0[i] = target;
                succ_count++;
                int idx = cfg_find_instr_index(instrs, instr_count, target);
                if (idx >= 0)
                {
                    cfg.is_block_start[idx] = true;
                    cfg.is_branch_target[idx] = true;
                }
                else
                {
                    fprintf(stderr, "cfg: missing branch target pc %d\n", target);
                }
            }
            else
            {
                fprintf(stderr, "cfg: failed to decode branch target at pc %d\n",
                        instr->pc);
            }
            if (i + 1 < instr_count)
            {
                cfg.succ_pc1[i] = instr->pc + instr->length;
                succ_count++;
            }
        }
        else if (cfg_is_unconditional_branch(instr->opcode))
        {
            int target = 0;
            if (cfg_decode_branch_target(instr, code, code_size, &target))
            {
                cfg.succ_pc0[i] = target;
                succ_count++;
                int idx = cfg_find_instr_index(instrs, instr_count, target);
                if (idx >= 0)
                {
                    cfg.is_block_start[idx] = true;
                    cfg.is_branch_target[idx] = true;
                }
                else
                {
                    fprintf(stderr, "cfg: missing branch target pc %d\n", target);
                }
            }
            else
            {
                fprintf(stderr, "cfg: failed to decode branch target at pc %d\n",
                        instr->pc);
            }
        }
        else if (!cfg_is_return_or_throw(instr->opcode))
        {
            if (i + 1 < instr_count)
            {
                cfg.succ_pc0[i] = instr->pc + instr->length;
                succ_count++;
            }
        }

        cfg.succ_count[i] = (uint8_t)succ_count;

        if (cfg_is_control_transfer(instr->opcode) && i + 1 < instr_count)
        {
            cfg.is_block_start[i + 1] = true;
        }
    }

    if (exceptions && exception_count > 0)
    {
        for (int i = 0; i < exception_count; ++i)
        {
            int idx = cfg_find_instr_index(instrs, instr_count,
                                           exceptions[i].handler_pc);
            if (idx >= 0)
            {
                cfg.is_block_start[idx] = true;
                cfg.is_handler_entry[idx] = true;
            }
            else
            {
                fprintf(stderr, "cfg: missing handler pc %d\n",
                        exceptions[i].handler_pc);
            }
        }
    }

    return cfg;
}

void cfg_free(CFG_Info *cfg)
{
    if (!cfg)
    {
        return;
    }

    (void)cfg;
}
