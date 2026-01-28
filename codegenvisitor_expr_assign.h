#pragma once

#include "cminor_base.h"
#include "visitor.h"

void enter_assignexpr(Expression *expr, Visitor *visitor);
void leave_assignexpr(Expression *expr, Visitor *visitor);
void enter_incexpr(Expression *expr, Visitor *visitor);
void leave_incexpr(Expression *expr, Visitor *visitor);
void enter_addrexpr(Expression *expr, Visitor *visitor);
void leave_addrexpr(Expression *expr, Visitor *visitor);
void leave_derefexpr(Expression *expr, Visitor *visitor);
