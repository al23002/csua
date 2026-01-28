#include <stdlib.h>
#include <string.h>

#include "method_code.h"

MethodCode *method_code_create()
{
    MethodCode *mc = (MethodCode *)calloc(1, sizeof(MethodCode));
    if (!mc)
    {
        return NULL;
    }
    return mc;
}

void method_code_destroy(MethodCode *mc)
{
    if (!mc)
    {
        return;
    }

    if (mc->code)
    {
        free(mc->code);
    }
    free(mc);
}

void method_code_reset(MethodCode *mc)
{
    if (!mc)
    {
        return;
    }

    mc->code_size = 0;
    mc->line_number_count = 0;
}

uint8_t *method_code_data(MethodCode *mc)
{
    return mc ? mc->code : NULL;
}

int method_code_size(MethodCode *mc)
{
    return mc ? mc->code_size : 0;
}

int method_code_capacity(MethodCode *mc)
{
    return mc ? mc->code_capacity : 0;
}

void method_code_write_u2_at(MethodCode *mc, int offset, int value)
{
    if (!mc || offset + 2 > mc->code_capacity)
    {
        return;
    }
    mc->code[offset + 0] = (uint8_t)(value >> 8);
    mc->code[offset + 1] = (uint8_t)value;
}

static void ensure_capacity(MethodCode *mc, int additional)
{
    if (mc->code_size + additional <= mc->code_capacity)
    {
        return;
    }

    int new_cap = mc->code_capacity ? mc->code_capacity * 2 : 32;
    while (new_cap < mc->code_size + additional)
    {
        new_cap *= 2;
    }

    uint8_t *new_code = (uint8_t *)calloc(new_cap, sizeof(uint8_t));
    if (mc->code && mc->code_size > 0)
    {
        memcpy(new_code, mc->code, mc->code_size);
    }
    mc->code = new_code;
    mc->code_capacity = new_cap;
}

void method_code_emit_u1(MethodCode *mc, int value)
{
    if (!mc)
    {
        return;
    }

    ensure_capacity(mc, 1);
    mc->code[mc->code_size++] = (uint8_t)value;
}

void method_code_emit_u2(MethodCode *mc, int value)
{
    if (!mc)
    {
        return;
    }

    ensure_capacity(mc, 2);
    mc->code[mc->code_size++] = (uint8_t)(value >> 8);
    mc->code[mc->code_size++] = (uint8_t)value;
}

void method_code_emit_u4(MethodCode *mc, int value)
{
    if (!mc)
    {
        return;
    }

    ensure_capacity(mc, 4);
    mc->code[mc->code_size++] = (uint8_t)(value >> 24);
    mc->code[mc->code_size++] = (uint8_t)(value >> 16);
    mc->code[mc->code_size++] = (uint8_t)(value >> 8);
    mc->code[mc->code_size++] = (uint8_t)value;
}

void method_code_add_line_number(MethodCode *mc, int line_number)
{
    if (!mc || line_number <= 0)
    {
        return;
    }

    int pc = mc->code_size;

    /* Skip if same line as previous entry at same PC */
    if (mc->line_number_count > 0)
    {
        LineNumberEntry *last = &mc->line_numbers[mc->line_number_count - 1];
        if (last->start_pc == pc || last->line_number == line_number)
        {
            return;
        }
    }

    /* Ensure capacity */
    if (mc->line_number_count >= mc->line_number_capacity)
    {
        int new_cap = mc->line_number_capacity ? mc->line_number_capacity * 2 : 16;
        LineNumberEntry *new_entries = (LineNumberEntry *)calloc(new_cap, sizeof(LineNumberEntry));
        if (mc->line_numbers && mc->line_number_count > 0)
        {
            /* Manual copy instead of memcpy (Cminor: sizeof not allowed with arithmetic) */
            for (int i = 0; i < mc->line_number_count; i++)
            {
                new_entries[i].start_pc = mc->line_numbers[i].start_pc;
                new_entries[i].line_number = mc->line_numbers[i].line_number;
            }
        }
        mc->line_numbers = new_entries;
        mc->line_number_capacity = new_cap;
    }

    mc->line_numbers[mc->line_number_count].start_pc = pc;
    mc->line_numbers[mc->line_number_count].line_number = line_number;
    mc->line_number_count++;
}

LineNumberEntry *method_code_line_numbers(MethodCode *mc)
{
    return mc ? mc->line_numbers : NULL;
}

int method_code_line_number_count(MethodCode *mc)
{
    return mc ? mc->line_number_count : 0;
}
