#pragma once

#include "cminor_base.h"
#include "visitor.h"

void enter_initializerlistexpr(Expression *expr, Visitor *visitor);
void leave_initializerlistexpr(Expression *expr, Visitor *visitor);
void enter_funccallexpr(Expression *expr, Visitor *visitor);
void leave_funccallexpr(Expression *expr, Visitor *visitor);
