#pragma once

#include <stdint.h>

/*
 * LineNumberEntry - mapping from bytecode offset to source line
 * Used for LineNumberTable attribute in class files
 */
typedef struct LineNumberEntry_tag
{
    int start_pc;    /* Bytecode offset */
    int line_number; /* Source line number */
} LineNumberEntry;

/*
 * MethodCode - Bytecode buffer for a single method
 *
 * Each method has its own Code attribute containing bytecode.
 * This structure holds the bytecode being generated for one method.
 */
typedef struct MethodCode_tag
{
    uint8_t *code;
    int code_size;
    int code_capacity;

    /* LineNumberTable entries */
    LineNumberEntry *line_numbers;
    int line_number_count;
    int line_number_capacity;
} MethodCode;

/* Create a new MethodCode */
MethodCode *method_code_create();

/* Destroy a MethodCode */
void method_code_destroy(MethodCode *mc);

/* Reset for new method (keeps allocated buffer) */
void method_code_reset(MethodCode *mc);

/* Accessors */
uint8_t *method_code_data(MethodCode *mc);
int method_code_size(MethodCode *mc);
int method_code_capacity(MethodCode *mc);

/* Write a 2-byte big-endian value at a specific offset (for patching) */
void method_code_write_u2_at(MethodCode *mc, int offset, int value);

/* Emit a single byte to the code buffer */
void method_code_emit_u1(MethodCode *mc, int value);

/* Emit a 2-byte big-endian value to the code buffer */
void method_code_emit_u2(MethodCode *mc, int value);

/* Emit a 4-byte big-endian value to the code buffer */
void method_code_emit_u4(MethodCode *mc, int value);

/* Add a line number entry at current PC */
void method_code_add_line_number(MethodCode *mc, int line_number);

/* Get line number entries */
LineNumberEntry *method_code_line_numbers(MethodCode *mc);
int method_code_line_number_count(MethodCode *mc);
