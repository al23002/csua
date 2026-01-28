#pragma once

#include "cminor_base.h"
#include "visitor.h"

void leave_addexpr(Expression *expr, Visitor *visitor);
void leave_subexpr(Expression *expr, Visitor *visitor);
void leave_mulexpr(Expression *expr, Visitor *visitor);
void leave_divexpr(Expression *expr, Visitor *visitor);
void leave_modexpr(Expression *expr, Visitor *visitor);
void leave_bit_and_expr(Expression *expr, Visitor *visitor);
void leave_bit_or_expr(Expression *expr, Visitor *visitor);
void leave_bit_xor_expr(Expression *expr, Visitor *visitor);
void leave_lshift_expr(Expression *expr, Visitor *visitor);
void leave_rshift_expr(Expression *expr, Visitor *visitor);
void leave_bit_not_expr(Expression *expr, Visitor *visitor);
void leave_unary_minus_expr(Expression *expr, Visitor *visitor);
void leave_unary_plus_expr(Expression *expr, Visitor *visitor);
void leave_logical_not_expr(Expression *expr, Visitor *visitor);
void leave_compareexpr(Expression *expr, Visitor *visitor);
void leave_castexpr(Expression *expr, Visitor *visitor);
void leave_typecastexpr(Expression *expr, Visitor *visitor);
void leave_array_to_pointer_expr(Expression *expr, Visitor *visitor);
void leave_conditionalexpr(Expression *expr, Visitor *visitor);
void leave_logical_and_expr(Expression *expr, Visitor *visitor);
void leave_logical_or_expr(Expression *expr, Visitor *visitor);
