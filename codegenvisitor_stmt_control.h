#pragma once

#include "cminor_base.h"
#include "visitor.h"

void enter_ifstmt(Statement *stmt, Visitor *visitor);
void leave_ifstmt(Statement *stmt, Visitor *visitor);
void enter_whilestmt(Statement *stmt, Visitor *visitor);
void leave_whilestmt(Statement *stmt, Visitor *visitor);
void enter_dowhilestmt(Statement *stmt, Visitor *visitor);
void leave_dowhilestmt(Statement *stmt, Visitor *visitor);
void enter_forstmt(Statement *stmt, Visitor *visitor);
void leave_forstmt(Statement *stmt, Visitor *visitor);
