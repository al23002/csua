#pragma once

#include "cminor_base.h"
#include "visitor.h"

void enter_generic_stmt(Statement *stmt, Visitor *visitor);
void leave_generic_stmt(Statement *stmt, Visitor *visitor);
void enter_compound_stmt(Statement *stmt, Visitor *visitor);
void leave_compound_stmt(Statement *stmt, Visitor *visitor);
void leave_exprstmt(Statement *stmt, Visitor *visitor);
