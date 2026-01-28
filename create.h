#pragma once

/*
 * create.h - AST node creation functions
 *
 * Used by the parser (parser.y) to construct AST nodes.
 * All creation functions that need location info require CS_Creator.
 */

#include "cminor_base.h"
#include "type_specifier.h"
#include "parsed_type.h"
#include "ast.h"

/* Identifier creation */
char *cs_create_identifier(const char *str);

/* Expression creation */
Expression *cs_create_int_expression(CS_Creator *creator, int v);
Expression *cs_create_uint_expression(CS_Creator *creator, int v);
Expression *cs_create_long_expression(CS_Creator *creator, long v);
Expression *cs_create_ulong_expression(CS_Creator *creator, long v);
Expression *cs_create_bool_expression(CS_Creator *creator, bool v);
Expression *cs_create_null_expression(CS_Creator *creator);
Expression *cs_create_double_expression(CS_Creator *creator, double v);
Expression *cs_create_float_expression(CS_Creator *creator, float v);
Expression *cs_create_string_expression(CS_Creator *creator, CS_String v);
Expression *cs_create_identifier_expression(CS_Creator *creator, char *identifier);
Expression *cs_create_inc_dec_expression(CS_Creator *creator, Expression *id_expr,
                                         ExpressionKind inc_dec, bool is_prefix);
Expression *cs_create_function_call_expression(CS_Creator *creator, Expression *function,
                                               ArgumentList *args);
Expression *cs_create_array_expression(CS_Creator *creator, Expression *array,
                                       Expression *index);
Expression *cs_create_member_expression(CS_Creator *creator, Expression *target,
                                        char *member, bool via_pointer);
Expression *cs_create_minus_expression(CS_Creator *creator, Expression *operand);
Expression *cs_create_plus_expression(CS_Creator *creator, Expression *operand);
Expression *cs_create_logical_not_expression(CS_Creator *creator, Expression *operand);
Expression *cs_create_bit_not_expression(CS_Creator *creator, Expression *operand);
Expression *cs_create_address_expression(CS_Creator *creator, Expression *operand);
Expression *cs_create_dereference_expression(CS_Creator *creator, Expression *operand);
Expression *cs_create_binary_expression(CS_Creator *creator, ExpressionKind kind,
                                        Expression *left, Expression *right);
Expression *cs_create_assignment_expression(CS_Creator *creator, Expression *left,
                                            AssignmentOperator aope,
                                            Expression *operand);
Expression *cs_create_conditional_expression(CS_Creator *creator, Expression *condition,
                                             Expression *true_expr,
                                             Expression *false_expr);
Expression *cs_create_comma_expression(CS_Creator *creator, Expression *left,
                                       Expression *right);
Expression *cs_create_cast_expression(CS_Creator *creator, CS_CastType ctype,
                                      Expression *operand);
Expression *cs_create_type_cast_expression(CS_Creator *creator, ParsedType *type,
                                           Expression *operand);
Expression *cs_create_sizeof_expression(CS_Creator *creator, ParsedType *type,
                                        Expression *expr, bool is_type);
Expression *cs_create_array_to_pointer_expression(CS_Creator *creator,
                                                  Expression *array_expr,
                                                  TypeSpecifier *ptr_type);
Expression *cs_create_initializer_list_expression(CS_Creator *creator,
                                                  ExpressionList *list);
Expression *cs_create_designated_initializer_expression(CS_Creator *creator,
                                                        char *field_name,
                                                        Expression *value);
ExpressionList *cs_chain_expression_list(ExpressionList *list, Expression *expr);

/* Statement creation */
Statement *cs_create_expression_statement(CS_Creator *creator, Expression *expr);
Statement *cs_create_declaration_statement(CS_Creator *creator, ParsedType *type,
                                           char *name, Expression *initializer,
                                           bool is_static);
Statement *cs_create_compound_statement(CS_Creator *creator, StatementList *list);
Statement *cs_create_if_statement(CS_Creator *creator, Expression *condition,
                                  Statement *then_stmt, Statement *else_stmt);
Statement *cs_create_while_statement(CS_Creator *creator, Expression *condition,
                                     Statement *body);
Statement *cs_create_do_while_statement(CS_Creator *creator, Statement *body,
                                        Expression *condition);
Statement *cs_create_for_statement(CS_Creator *creator, Statement *init,
                                   Expression *condition, Expression *post,
                                   Statement *body);
Statement *cs_create_switch_statement(CS_Creator *creator, Expression *expression,
                                      Statement *body);
Statement *cs_create_case_statement(CS_Creator *creator, Expression *expression,
                                    Statement *statement);
Statement *cs_create_default_statement(CS_Creator *creator, Statement *statement);
Statement *cs_create_goto_statement(CS_Creator *creator, char *label);
Statement *cs_create_label_statement(CS_Creator *creator, char *label,
                                     Statement *statement);
Statement *cs_create_break_statement(CS_Creator *creator);
Statement *cs_create_continue_statement(CS_Creator *creator);
Statement *cs_create_return_statement(CS_Creator *creator, Expression *expression);
StatementList *cs_create_statement_list(Statement *stmt);
StatementList *cs_chain_statement_list(StatementList *stmt_list, Statement *stmt);

/* Type creation */
TypeSpecifier *cs_create_type_specifier(CS_BasicType type);
TypeSpecifier *cs_create_named_type_specifier(CS_BasicType type, char *user_type_name);
TypeSpecifier *cs_copy_type_specifier(TypeSpecifier *type);
TypeSpecifier *cs_wrap_pointer(TypeSpecifier *base, int pointer_level);
TypeSpecifier *cs_wrap_array(TypeSpecifier *base, Expression *array_size);

/* Declaration creation */
DeclarationList *cs_create_declaration_list(Declaration *decl);
DeclarationList *cs_chain_declaration(DeclarationList *decl_list, Declaration *decl);

/* Function declaration */
FunctionDeclaration *cs_create_function_declaration(CS_Creator *creator,
                                                    ParsedType *type,
                                                    char *name,
                                                    ParameterList *param,
                                                    bool is_variadic,
                                                    bool is_static,
                                                    AttributeSpecifier *attributes,
                                                    Statement *body);
/* Parameter and argument creation */
ParameterList *cs_create_parameter(CS_Creator *creator, ParsedType *type,
                                   char *name, bool is_ellipsis);
ParameterList *cs_chain_parameter_list(ParameterList *list, ParameterList *param);
ArgumentList *cs_create_argument(Expression *expr);
ArgumentList *cs_chain_argument_list(ArgumentList *list, Expression *expr);

/* Attribute creation */
AttributeSpecifier *cs_create_attribute(const char *raw_text);
AttributeSpecifier *cs_chain_attribute(AttributeSpecifier *list,
                                       AttributeSpecifier *attr);
