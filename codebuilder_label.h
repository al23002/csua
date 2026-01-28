#pragma once

#include "codebuilder_defs.h"

/* Label operations (internal) */
void cb_ensure_label_capacity(CodeBuilder *builder);
void cb_ensure_pending_jump_capacity(CodeBuilder *builder);
void cb_add_pending_jump(CodeBuilder *builder, int jump_pc, CB_Label *target);
void cb_write_s2_at_pc(CodeBuilder *builder, int pc, int value);

void codebuilder_record_branch_target(CodeBuilder *builder, int target_pc);
void codebuilder_record_branch_target_with_frame(CodeBuilder *builder, int target_pc, CB_Frame *frame);
void codebuilder_record_exception_handler(CodeBuilder *builder, int handler_pc,
                                          const char *exception_class);

CB_Label *codebuilder_create_label(CodeBuilder *builder);
void codebuilder_place_label(CodeBuilder *builder, CB_Label *label);
void codebuilder_mark_loop_header(CodeBuilder *builder, CB_Label *label);
void codebuilder_mark_jump_only(CodeBuilder *builder, CB_Label *label);
void codebuilder_set_jump_context(CodeBuilder *builder, const char *context);
void codebuilder_jump(CodeBuilder *builder, CB_Label *target);
void codebuilder_jump_if(CodeBuilder *builder, CB_Label *target);
void codebuilder_jump_if_not(CodeBuilder *builder, CB_Label *target);
void codebuilder_jump_if_op(CodeBuilder *builder, IfCond cond, CB_Label *target);
void codebuilder_jump_if_icmp(CodeBuilder *builder, IntCmpCond cond, CB_Label *target);
void codebuilder_jump_if_acmp(CodeBuilder *builder, ACmpCond cond, CB_Label *target);
void codebuilder_jump_if_null(CodeBuilder *builder, CB_Label *target);
void codebuilder_jump_if_not_null(CodeBuilder *builder, CB_Label *target);
void codebuilder_resolve_jumps(CodeBuilder *builder);
int codebuilder_current_pc(CodeBuilder *builder);

/* Label diagnostics */
void codebuilder_set_label_name(CB_Label *label, const char *name);
void codebuilder_dump_label_info(CodeBuilder *builder, CB_Label *label);
void codebuilder_dump_all_labels(CodeBuilder *builder);
void codebuilder_diagnose_frame_issues(CodeBuilder *builder);
