#pragma once

#include "codebuilder_defs.h"
#include "classfile.h"

/* Stack operations - direct type tracking */
void cb_push(CodeBuilder *builder, CB_VerificationType type);
CB_VerificationType cb_pop(CodeBuilder *builder);

/* Update max tracking */
void cb_update_max_stack(CodeBuilder *builder);
void cb_update_max_locals(CodeBuilder *builder, int index);

/* Set stack depth (for control flow) */
void cb_set_stack_depth(CodeBuilder *builder, int depth);

/* Frame operations */
CB_Frame *cb_create_frame();
void cb_copy_frame(CB_Frame *dest, const CB_Frame *src);
void cb_merge_frame(CB_Frame *dest, const CB_Frame *src);

/* Safe frame restoration - copies frame and syncs max_stack */
void codebuilder_restore_frame_safe(CodeBuilder *builder, const CB_Frame *saved);

void codebuilder_set_stack(CodeBuilder *builder, int value);
int codebuilder_current_stack(CodeBuilder *builder);
CodebuilderStackMark codebuilder_mark_stack(CodeBuilder *builder);
void codebuilder_restore_stack(CodeBuilder *builder, CodebuilderStackMark mark);

void codebuilder_begin_block(CodeBuilder *builder);
void codebuilder_end_block(CodeBuilder *builder);
int codebuilder_allocate_local(CodeBuilder *builder, CB_VerificationType type);
int codebuilder_current_locals(CodeBuilder *builder);

void codebuilder_set_local(CodeBuilder *builder, int index, CB_VerificationType type);
void codebuilder_set_param(CodeBuilder *builder, int index, CB_VerificationType type);
CB_VerificationType codebuilder_get_local(CodeBuilder *builder, int index);

/* Diagnostics */
void codebuilder_print_diagnostics(CodeBuilder *builder);
bool codebuilder_has_errors(CodeBuilder *builder);
void cb_set_merge_verbose(bool verbose);
