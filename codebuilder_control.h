#pragma once

#include "codebuilder_defs.h"

/* Control stack operations (internal) */
void cb_ensure_control_capacity(CodeBuilder *builder);
CB_ControlEntry *cb_push_control(CodeBuilder *builder, CB_ControlKind kind);
CB_ControlEntry *cb_top_control(CodeBuilder *builder);
CB_ControlEntry *cb_pop_control(CodeBuilder *builder, CB_ControlKind expected);
CB_ControlEntry *cb_find_loop_or_switch(CodeBuilder *builder);
CB_ControlEntry *cb_find_loop(CodeBuilder *builder);

void codebuilder_begin_if(CodeBuilder *builder);
void codebuilder_if_then(CodeBuilder *builder);
void codebuilder_if_else(CodeBuilder *builder);
void codebuilder_end_if(CodeBuilder *builder);

void codebuilder_begin_while(CodeBuilder *builder);
void codebuilder_while_body(CodeBuilder *builder);
void codebuilder_end_while(CodeBuilder *builder);

void codebuilder_begin_do_while(CodeBuilder *builder);
void codebuilder_do_while_cond(CodeBuilder *builder);
void codebuilder_end_do_while(CodeBuilder *builder);

void codebuilder_begin_for(CodeBuilder *builder);
void codebuilder_for_cond(CodeBuilder *builder);
void codebuilder_for_body(CodeBuilder *builder);
void codebuilder_for_post(CodeBuilder *builder);
void codebuilder_end_for(CodeBuilder *builder);

void codebuilder_emit_break(CodeBuilder *builder);
void codebuilder_emit_continue(CodeBuilder *builder);

void codebuilder_begin_switch(CodeBuilder *builder);
void codebuilder_switch_dispatch(CodeBuilder *builder, int expr_local);
void codebuilder_switch_case(CodeBuilder *builder, int value);
void codebuilder_switch_default(CodeBuilder *builder);
void codebuilder_end_switch(CodeBuilder *builder);
int codebuilder_switch_expr_local(CodeBuilder *builder);

CB_ControlEntry *codebuilder_push_loop_raw(CodeBuilder *builder);
void codebuilder_pop_loop_raw(CodeBuilder *builder);
CB_ControlEntry *codebuilder_current_loop(CodeBuilder *builder);
CB_ControlEntry *codebuilder_push_switch_raw(CodeBuilder *builder);
void codebuilder_pop_switch_raw(CodeBuilder *builder);
CB_ControlEntry *codebuilder_current_switch(CodeBuilder *builder);
void codebuilder_switch_add_case(CodeBuilder *builder, int value, CB_Label *label);

bool codebuilder_should_use_tableswitch(int nlabels, int low, int high);
void codebuilder_build_tableswitch(CodeBuilder *builder,
                                   CB_Label *default_label,
                                   int low, int high,
                                   CB_Label **jump_table);
void codebuilder_build_lookupswitch(CodeBuilder *builder,
                                    CB_Label *default_label,
                                    int npairs,
                                    const int *keys,
                                    CB_Label **targets);
