#pragma once

#include "cminor_base.h"
#include "visitor.h"

void enter_switchstmt(Statement *stmt, Visitor *visitor);
void leave_switchstmt(Statement *stmt, Visitor *visitor);
void enter_casestmt(Statement *stmt, Visitor *visitor);
void leave_casestmt(Statement *stmt, Visitor *visitor);
void enter_defaultstmt(Statement *stmt, Visitor *visitor);
void leave_defaultstmt(Statement *stmt, Visitor *visitor);
void leave_returnstmt(Statement *stmt, Visitor *visitor);
void leave_breakstmt(Statement *stmt, Visitor *visitor);
void leave_continuestmt(Statement *stmt, Visitor *visitor);
void enter_labelstmt(Statement *stmt, Visitor *visitor);
void leave_labelstmt(Statement *stmt, Visitor *visitor);
void leave_gotostmt(Statement *stmt, Visitor *visitor);
