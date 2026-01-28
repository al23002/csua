#pragma once

#include "codebuilder_defs.h"

CodeBuilder *codebuilder_create(ConstantPoolBuilder *cp, MethodCode *method,
                                bool is_static, const char *class_name,
                                ParameterList *params, const char *method_name);
void codebuilder_destroy(CodeBuilder *builder);

/*
 * Reachability tracking API (javac-style alive flag)
 *
 * Used to track whether the current code position is reachable.
 * After unconditional jumps (goto, return, throw, break, continue),
 * code becomes unreachable until a label that can be reached is placed.
 */

/* Check if current code position is reachable */
bool codebuilder_is_alive(CodeBuilder *builder);

/* Mark current position as unreachable (after goto/return/throw) */
void codebuilder_mark_dead(CodeBuilder *builder);

/* Mark current position as reachable (when placing a reachable label) */
void codebuilder_mark_alive(CodeBuilder *builder);
