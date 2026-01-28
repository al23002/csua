#pragma once

#include "cminor_base.h"
#include "visitor.h"

void enter_noop_expr(Expression *expr, Visitor *visitor);
void leave_noop_expr(Expression *expr, Visitor *visitor);
void enter_intexpr(Expression *expr, Visitor *visitor);
void leave_intexpr(Expression *expr, Visitor *visitor);
void enter_longexpr(Expression *expr, Visitor *visitor);
void leave_longexpr(Expression *expr, Visitor *visitor);
void enter_floatexpr(Expression *expr, Visitor *visitor);
void leave_floatexpr(Expression *expr, Visitor *visitor);
void enter_doubleexpr(Expression *expr, Visitor *visitor);
void leave_doubleexpr(Expression *expr, Visitor *visitor);
void enter_boolexpr(Expression *expr, Visitor *visitor);
void leave_boolexpr(Expression *expr, Visitor *visitor);
void enter_nullexpr(Expression *expr, Visitor *visitor);
void leave_nullexpr(Expression *expr, Visitor *visitor);
void enter_stringexpr(Expression *expr, Visitor *visitor);
void leave_stringexpr(Expression *expr, Visitor *visitor);
void leave_memberexpr(Expression *expr, Visitor *visitor);
void enter_identifierexpr(Expression *expr, Visitor *visitor);
void leave_identifierexpr(Expression *expr, Visitor *visitor);
void leave_arrayexpr(Expression *expr, Visitor *visitor);
void enter_sizeofexpr(Expression *expr, Visitor *visitor);
void leave_sizeofexpr(Expression *expr, Visitor *visitor);
