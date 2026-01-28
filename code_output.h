#pragma once

#include <stdint.h>
#include "constant_pool.h"
#include "method_code.h"

/*
 * CodeOutput - Combined bytecode and constant pool output
 *
 * This structure combines ConstantPoolBuilder (class-level, shared)
 * and MethodCode (method-level, per-method).
 */
typedef struct CodeOutput_tag
{
    MethodCode *method;
    ConstantPoolBuilder *cp;
    bool owns_cp; /* true if we own the CP and should destroy it */
} CodeOutput;

/* Create a new CodeOutput (owns its own constant pool) */
CodeOutput *code_output_create();

/* Create a CodeOutput with external constant pool (borrowed, not owned) */
CodeOutput *code_output_create_with_cp(ConstantPoolBuilder *cp);

/* Destroy a CodeOutput (only destroys CP if owned) */
void code_output_destroy(CodeOutput *out);

/* Check if this CodeOutput owns its constant pool */
bool code_output_owns_cp(CodeOutput *out);

/* Take ownership of the constant pool (caller becomes owner) */
ConstantPoolBuilder *code_output_take_cp(CodeOutput *out);

/* Accessors */
ConstantPoolBuilder *code_output_cp(CodeOutput *out);
MethodCode *code_output_method(CodeOutput *out);

/* Reset method code for new method (keeps constant pool) */
void code_output_reset_method(CodeOutput *out);
