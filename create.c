#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ascii.h"
#include "ast.h"
#include "definitions.h"
#include "compiler.h"
#include "create.h"
#include "util.h"
#include "cminor_type.h"
#include "parsed_type.h"

static Expression *cs_create_expression(CS_Creator *creator, ExpressionKind ekind)
{
    Expression *expr = (Expression *)calloc(1, sizeof(Expression));
    expr->kind = ekind;
    expr->type = NULL;
    expr->parsed_type = NULL;
    expr->line_number = creator ? creator->line_number : 1;
    if (creator && creator->source_path)
    {
        expr->input_location.path = cs_create_identifier(creator->source_path);
    }
    else
    {
        expr->input_location.path = NULL;
    }
    expr->input_location.line = expr->line_number;
    return expr;
}

Expression *cs_create_int_expression(CS_Creator *creator, int v)
{
    Expression *expr = cs_create_expression(creator, INT_EXPRESSION);
    expr->u.int_value = v;
    return expr;
}

Expression *cs_create_uint_expression(CS_Creator *creator, int v)
{
    Expression *expr = cs_create_expression(creator, UINT_EXPRESSION);
    expr->u.int_value = v;
    return expr;
}

Expression *cs_create_long_expression(CS_Creator *creator, long v)
{
    Expression *expr = cs_create_expression(creator, LONG_EXPRESSION);
    expr->u.long_value = v;
    return expr;
}

Expression *cs_create_ulong_expression(CS_Creator *creator, long v)
{
    Expression *expr = cs_create_expression(creator, ULONG_EXPRESSION);
    expr->u.long_value = v;
    return expr;
}

Expression *cs_create_bool_expression(CS_Creator *creator, bool v)
{
    Expression *expr = cs_create_expression(creator, BOOL_EXPRESSION);
    expr->u.bool_value = v;
    return expr;
}

Expression *cs_create_null_expression(CS_Creator *creator)
{
    Expression *expr = cs_create_expression(creator, NULL_EXPRESSION);
    return expr;
}

ExpressionList *cs_chain_expression_list(ExpressionList *list, Expression *expr)
{
    ExpressionList *p = list;
    ExpressionList *nlist =
        (ExpressionList *)calloc(1, sizeof(ExpressionList));
    nlist->next = NULL;
    nlist->expression = expr;
    if (p != NULL)
    {
        while (p->next)
            p = p->next;
        p->next = nlist;
        return list;
    }
    return nlist;
}

Expression *cs_create_double_expression(CS_Creator *creator, double v)
{
    Expression *expr = cs_create_expression(creator, DOUBLE_EXPRESSION);
    expr->u.double_value = v;
    return expr;
}

Expression *cs_create_float_expression(CS_Creator *creator, float v)
{
    Expression *expr = cs_create_expression(creator, FLOAT_EXPRESSION);
    expr->u.float_value = v;
    return expr;
}

Expression *cs_create_string_expression(CS_Creator *creator, CS_String v)
{
    Expression *expr = cs_create_expression(creator, STRING_EXPRESSION);
    expr->u.string_value = v;
    return expr;
}

Expression *cs_create_identifier_expression(CS_Creator *creator, char *identifier)
{
    Expression *expr = cs_create_expression(creator, IDENTIFIER_EXPRESSION);
    expr->u.identifier.name = identifier;
    expr->u.identifier.is_function = false;
    expr->u.identifier.is_enum_member = false;
    expr->u.identifier.u.declaration = NULL;
    return expr;
}

Expression *cs_create_inc_dec_expression(CS_Creator *creator, Expression *id_expr,
                                         ExpressionKind inc_dec, bool is_prefix)
{
    Expression *expr = cs_create_expression(creator, inc_dec);
    expr->u.inc_dec.target = id_expr;
    expr->u.inc_dec.is_prefix = is_prefix;
    return expr;
}

Expression *cs_create_function_call_expression(CS_Creator *creator, Expression *function,
                                               ArgumentList *args)
{
    Expression *expr = cs_create_expression(creator, FUNCTION_CALL_EXPRESSION);
    expr->u.function_call_expression.function = function;
    expr->u.function_call_expression.argument = args;
    return expr;
}

Expression *cs_create_array_expression(CS_Creator *creator, Expression *array,
                                       Expression *index)
{
    Expression *expr = cs_create_expression(creator, ARRAY_EXPRESSION);
    expr->u.array_expression.array = array;
    expr->u.array_expression.index = index;
    return expr;
}

Expression *cs_create_member_expression(CS_Creator *creator, Expression *target,
                                        char *member, bool via_pointer)
{
    Expression *expr = cs_create_expression(creator, MEMBER_EXPRESSION);
    expr->u.member_expression.target = target;
    expr->u.member_expression.member_name = member;
    expr->u.member_expression.via_pointer = via_pointer;
    return expr;
}

Expression *cs_create_minus_expression(CS_Creator *creator, Expression *operand)
{
    /* Constant folding for literals */
    if (operand->kind == INT_EXPRESSION)
    {
        int val = operand->u.int_value;
        /* -INT_MIN would overflow, so promote to long */
        if (val == (-2147483647 - 1))
        {
            return cs_create_long_expression(creator, 2147483648L);
        }
        return cs_create_int_expression(creator, -val);
    }
    if (operand->kind == LONG_EXPRESSION)
    {
        long val = operand->u.long_value;
        long neg = -val;
        /* If result fits in int range, demote to int */
        if (neg >= (-2147483647L - 1) && neg <= 2147483647L)
        {
            return cs_create_int_expression(creator, (int)neg);
        }
        return cs_create_long_expression(creator, neg);
    }

    Expression *expr = cs_create_expression(creator, MINUS_EXPRESSION);
    expr->u.minus_expression = operand;
    return expr;
}

Expression *cs_create_plus_expression(CS_Creator *creator, Expression *operand)
{
    /* Unary plus: no constant folding needed, just create the expression.
     * Type promotion is handled during semantic analysis. */
    Expression *expr = cs_create_expression(creator, PLUS_EXPRESSION);
    expr->u.plus_expression = operand;
    return expr;
}

Expression *cs_create_logical_not_expression(CS_Creator *creator, Expression *operand)
{
    Expression *expr = cs_create_expression(creator, LOGICAL_NOT_EXPRESSION);
    expr->u.logical_not_expression = operand;
    return expr;
}

Expression *cs_create_bit_not_expression(CS_Creator *creator, Expression *operand)
{
    Expression *expr = cs_create_expression(creator, BIT_NOT_EXPRESSION);
    expr->u.bit_not_expression = operand;
    return expr;
}

Expression *cs_create_address_expression(CS_Creator *creator, Expression *operand)
{
    Expression *expr = cs_create_expression(creator, ADDRESS_EXPRESSION);
    expr->u.address_expression = operand;
    return expr;
}

Expression *cs_create_dereference_expression(CS_Creator *creator, Expression *operand)
{
    Expression *expr = cs_create_expression(creator, DEREFERENCE_EXPRESSION);
    expr->u.dereference_expression = operand;
    return expr;
}

Expression *cs_create_binary_expression(CS_Creator *creator, ExpressionKind kind,
                                        Expression *left, Expression *right)
{
    Expression *expr = cs_create_expression(creator, kind);
    expr->u.binary_expression.left = left;
    expr->u.binary_expression.right = right;
    return expr;
}

Expression *cs_create_assignment_expression(CS_Creator *creator, Expression *left,
                                            AssignmentOperator aope,
                                            Expression *operand)
{
    Expression *expr = cs_create_expression(creator, ASSIGN_EXPRESSION);
    expr->u.assignment_expression.aope = aope;
    expr->u.assignment_expression.left = left;
    expr->u.assignment_expression.right = operand;
    return expr;
}

Expression *cs_create_conditional_expression(CS_Creator *creator, Expression *condition,
                                             Expression *true_expr,
                                             Expression *false_expr)
{
    Expression *expr = cs_create_expression(creator, CONDITIONAL_EXPRESSION);
    expr->u.conditional_expression.condition = condition;
    expr->u.conditional_expression.true_expr = true_expr;
    expr->u.conditional_expression.false_expr = false_expr;
    return expr;
}

Expression *cs_create_comma_expression(CS_Creator *creator, Expression *left,
                                       Expression *right)
{
    Expression *expr = cs_create_expression(creator, COMMA_EXPRESSION);
    expr->u.comma_expression.left = left;
    expr->u.comma_expression.right = right;
    return expr;
}

Expression *cs_create_cast_expression(CS_Creator *creator, CS_CastType ctype,
                                      Expression *operand)
{
    Expression *expr = cs_create_expression(creator, CAST_EXPRESSION);
    expr->u.cast_expression.ctype = ctype;
    expr->u.cast_expression.expr = operand;
    return expr;
}

Expression *cs_create_type_cast_expression(CS_Creator *creator, ParsedType *type,
                                           Expression *operand)
{
    Expression *expr = cs_create_expression(creator, TYPE_CAST_EXPRESSION);
    expr->parsed_type = cs_copy_parsed_type(type);
    expr->u.type_cast_expression.expr = operand;
    return expr;
}

Expression *cs_create_sizeof_expression(CS_Creator *creator, ParsedType *type,
                                        Expression *expr, bool is_type)
{
    Expression *sizeof_expr = cs_create_expression(creator, SIZEOF_EXPRESSION);
    sizeof_expr->parsed_type = cs_copy_parsed_type(type);
    sizeof_expr->u.sizeof_expression.expr = expr;
    sizeof_expr->u.sizeof_expression.is_type = is_type;
    return sizeof_expr;
}

Expression *cs_create_array_to_pointer_expression(CS_Creator *creator,
                                                  Expression *array_expr,
                                                  TypeSpecifier *ptr_type)
{
    Expression *expr = cs_create_expression(creator, ARRAY_TO_POINTER_EXPRESSION);
    expr->u.array_to_pointer = array_expr;
    expr->type = cs_copy_type_specifier(ptr_type);
    return expr;
}

char *cs_create_identifier(const char *str)
{
    return strdup(str);
}

/* Extract a quoted string and advance cursor past the closing quote */
static char *extract_quoted_string(const char **cursor_ptr)
{
    const char *cursor = *cursor_ptr;
    while (*cursor && ascii_is_space((unsigned char)(*cursor)))
    {
        ++cursor;
    }
    if (*cursor != '"')
    {
        return NULL;
    }
    ++cursor;
    const char *start = cursor;
    while (*cursor && *cursor != '"')
    {
        ++cursor;
    }
    if (*cursor != '"')
    {
        return NULL;
    }
    int len = (int)(cursor - start);
    char *str = (char *)calloc(len + 1, sizeof(char));
    strncpy(str, start, len);
    str[len] = '\0';
    ++cursor; /* skip closing quote */
    *cursor_ptr = cursor;
    return str;
}

/* Extract a single quoted string for cminor::new
 * Format: cminor::new("class")
 */
static bool extract_java_class(const char *raw, const char *prefix,
                               char **class_name)
{
    int prefix_len = strlen(prefix);
    if (!raw || strncmp(raw, prefix, prefix_len) != 0)
    {
        return false;
    }

    const char *cursor = raw + prefix_len;
    while (*cursor && ascii_is_space((unsigned char)(*cursor)))
    {
        ++cursor;
    }
    if (*cursor != '(')
    {
        return false;
    }
    ++cursor;

    *class_name = extract_quoted_string(&cursor);
    if (!*class_name)
    {
        return false;
    }

    return true;
}

/* Extract three comma-separated quoted strings for get_static/invoke_virtual
 * Format: cminor::get_static("class", "member", "descriptor")
 */
static bool extract_java_ref(const char *raw, const char *prefix,
                             char **class_name, char **member_name,
                             char **descriptor)
{
    int prefix_len = strlen(prefix);
    if (!raw || strncmp(raw, prefix, prefix_len) != 0)
    {
        return false;
    }

    const char *cursor = raw + prefix_len;
    while (*cursor && ascii_is_space((unsigned char)(*cursor)))
    {
        ++cursor;
    }
    if (*cursor != '(')
    {
        return false;
    }
    ++cursor;

    /* First string: class name */
    *class_name = extract_quoted_string(&cursor);
    if (!*class_name)
    {
        return false;
    }

    /* Skip comma */
    while (*cursor && ascii_is_space((unsigned char)(*cursor)))
    {
        ++cursor;
    }
    if (*cursor != ',')
    {
        return false;
    }
    ++cursor;

    /* Second string: member name */
    *member_name = extract_quoted_string(&cursor);
    if (!*member_name)
    {
        return false;
    }

    /* Skip comma */
    while (*cursor && ascii_is_space((unsigned char)(*cursor)))
    {
        ++cursor;
    }
    if (*cursor != ',')
    {
        return false;
    }
    ++cursor;

    /* Third string: descriptor */
    *descriptor = extract_quoted_string(&cursor);
    if (!*descriptor)
    {
        return false;
    }

    return true;
}

AttributeSpecifier *cs_create_attribute(const char *raw_text)
{
    AttributeSpecifier *attr = (AttributeSpecifier *)calloc(1, sizeof(AttributeSpecifier));
    attr->kind = CS_ATTRIBUTE_UNKNOWN;
    attr->text = cs_create_identifier(raw_text ? raw_text : "");
    attr->class_name = NULL;
    attr->member_name = NULL;
    attr->descriptor = NULL;
    attr->next = NULL;

    if (raw_text)
    {
        /* Use local variables for extract_java_ref results.
         * In Cminor, &attr->field doesn't work correctly (creates copy, not reference).
         * Local variables with & work due to heap-lifting. */
        char *class_name = NULL;
        char *member_name = NULL;
        char *descriptor = NULL;

        if (strncmp(raw_text, "deprecated", strlen("deprecated")) == 0)
        {
            attr->kind = CS_ATTRIBUTE_DEPRECATED;
        }
        else if (extract_java_ref(raw_text, "cminor::get_static",
                                  &class_name, &member_name, &descriptor))
        {
            attr->kind = CS_ATTRIBUTE_GET_STATIC;
            attr->class_name = class_name;
            attr->member_name = member_name;
            attr->descriptor = descriptor;
        }
        else if (extract_java_ref(raw_text, "cminor::invoke_virtual",
                                  &class_name, &member_name, &descriptor))
        {
            attr->kind = CS_ATTRIBUTE_INVOKE_VIRTUAL;
            attr->class_name = class_name;
            attr->member_name = member_name;
            attr->descriptor = descriptor;
        }
        else if (extract_java_ref(raw_text, "cminor::invoke_static",
                                  &class_name, &member_name, &descriptor))
        {
            attr->kind = CS_ATTRIBUTE_INVOKE_STATIC;
            attr->class_name = class_name;
            attr->member_name = member_name;
            attr->descriptor = descriptor;
        }
        else if (extract_java_ref(raw_text, "cminor::invoke_special",
                                  &class_name, &member_name, &descriptor))
        {
            attr->kind = CS_ATTRIBUTE_INVOKE_SPECIAL;
            attr->class_name = class_name;
            attr->member_name = member_name;
            attr->descriptor = descriptor;
        }
        else if (extract_java_ref(raw_text, "cminor::get_field",
                                  &class_name, &member_name, &descriptor))
        {
            attr->kind = CS_ATTRIBUTE_GET_FIELD;
            attr->class_name = class_name;
            attr->member_name = member_name;
            attr->descriptor = descriptor;
        }
        else if (extract_java_class(raw_text, "cminor::new", &class_name))
        {
            attr->kind = CS_ATTRIBUTE_NEW;
            attr->class_name = class_name;
        }
        else if (strncmp(raw_text, "cminor::arraylength", strlen("cminor::arraylength")) == 0)
        {
            attr->kind = CS_ATTRIBUTE_ARRAYLENGTH;
        }
        else if (strncmp(raw_text, "cminor::aaload", strlen("cminor::aaload")) == 0)
        {
            attr->kind = CS_ATTRIBUTE_AALOAD;
        }
        else if (strncmp(raw_text, "cminor::clinit", strlen("cminor::clinit")) == 0)
        {
            attr->kind = CS_ATTRIBUTE_CLINIT;
        }
    }

    return attr;
}

AttributeSpecifier *cs_chain_attribute(AttributeSpecifier *list,
                                       AttributeSpecifier *attr)
{
    if (!list)
    {
        return attr;
    }
    AttributeSpecifier *tail = list;
    while (tail->next)
    {
        tail = tail->next;
    }
    tail->next = attr;
    return list;
}

/* For Statement */
static Statement *cs_create_statement(CS_Creator *creator, StatementType type)
{
    Statement *stmt = (Statement *)calloc(1, sizeof(Statement));
    stmt->type = type;
    stmt->line_number = creator ? creator->line_number : 1;
    return stmt;
}

Statement *cs_create_expression_statement(CS_Creator *creator, Expression *expr)
{
    Statement *stmt = cs_create_statement(creator, EXPRESSION_STATEMENT);
    stmt->u.expression_s = expr;
    return stmt;
}

Statement *cs_create_compound_statement(CS_Creator *creator, StatementList *list)
{
    Statement *stmt = cs_create_statement(creator, COMPOUND_STATEMENT);
    stmt->u.compound_s.list = list;
    return stmt;
}

Statement *cs_create_if_statement(CS_Creator *creator, Expression *condition,
                                  Statement *then_stmt, Statement *else_stmt)
{
    Statement *stmt = cs_create_statement(creator, IF_STATEMENT);
    stmt->u.if_s.condition = condition;
    stmt->u.if_s.then_statement = then_stmt;
    stmt->u.if_s.else_statement = else_stmt;
    return stmt;
}

Statement *cs_create_while_statement(CS_Creator *creator, Expression *condition,
                                     Statement *body)
{
    Statement *stmt = cs_create_statement(creator, WHILE_STATEMENT);
    stmt->u.while_s.condition = condition;
    stmt->u.while_s.body = body;
    return stmt;
}

Statement *cs_create_do_while_statement(CS_Creator *creator, Statement *body,
                                        Expression *condition)
{
    Statement *stmt = cs_create_statement(creator, DO_WHILE_STATEMENT);
    stmt->u.do_s.body = body;
    stmt->u.do_s.condition = condition;
    return stmt;
}

Statement *cs_create_for_statement(CS_Creator *creator, Statement *init,
                                   Expression *condition, Expression *post,
                                   Statement *body)
{
    Statement *stmt = cs_create_statement(creator, FOR_STATEMENT);
    stmt->u.for_s.init = init;
    stmt->u.for_s.condition = condition;
    stmt->u.for_s.post = post;
    stmt->u.for_s.body = body;
    return stmt;
}

Statement *cs_create_switch_statement(CS_Creator *creator, Expression *expression,
                                      Statement *body)
{
    Statement *stmt = cs_create_statement(creator, SWITCH_STATEMENT);
    stmt->u.switch_s.expression = expression;
    stmt->u.switch_s.body = body;
    return stmt;
}

Statement *cs_create_case_statement(CS_Creator *creator, Expression *expression,
                                    Statement *statement)
{
    Statement *stmt = cs_create_statement(creator, CASE_STATEMENT);
    stmt->u.case_s.expression = expression;
    stmt->u.case_s.statement = statement;
    return stmt;
}

Statement *cs_create_default_statement(CS_Creator *creator, Statement *statement)
{
    Statement *stmt = cs_create_statement(creator, DEFAULT_STATEMENT);
    stmt->u.default_s.statement = statement;
    return stmt;
}

Statement *cs_create_goto_statement(CS_Creator *creator, char *label)
{
    Statement *stmt = cs_create_statement(creator, GOTO_STATEMENT);
    stmt->u.goto_s.label = label;
    return stmt;
}

Statement *cs_create_label_statement(CS_Creator *creator, char *label,
                                     Statement *statement)
{
    Statement *stmt = cs_create_statement(creator, LABEL_STATEMENT);
    stmt->u.label_s.label = label;
    stmt->u.label_s.statement = statement;
    return stmt;
}

Statement *cs_create_break_statement(CS_Creator *creator)
{
    return cs_create_statement(creator, BREAK_STATEMENT);
}

Statement *cs_create_continue_statement(CS_Creator *creator)
{
    return cs_create_statement(creator, CONTINUE_STATEMENT);
}

Statement *cs_create_return_statement(CS_Creator *creator, Expression *expression)
{
    Statement *stmt = cs_create_statement(creator, RETURN_STATEMENT);
    stmt->u.return_s.expression = expression;
    return stmt;
}

ParameterList *cs_create_parameter(CS_Creator *creator, ParsedType *type,
                                   char *name, bool is_ellipsis)
{
    ParameterList *param = (ParameterList *)calloc(1, sizeof(ParameterList));
    param->type = NULL;
    param->parsed_type = cs_copy_parsed_type(type);
    param->name = name;
    param->line_number = creator ? creator->line_number : 1;
    param->is_ellipsis = is_ellipsis;
    param->next = NULL;
    return param;
}

static Declaration *cs_create_declaration(CS_Creator *creator, ParsedType *type,
                                          char *name, Expression *initializer,
                                          bool is_static)
{
    Declaration *decl = (Declaration *)calloc(1, sizeof(Declaration));
    decl->type = NULL;
    decl->parsed_type = cs_copy_parsed_type(type);
    decl->name = name;
    decl->initializer = initializer;
    const char *path = creator ? creator->source_path : NULL;
    decl->source_path = path ? strdup(path) : NULL;
    decl->class_name = NULL; /* Set by header_decl_add_declaration from FileDecl */
    decl->index = -1;
    decl->needs_heap_lift = false;
    decl->is_static = is_static;
    return decl;
}

Statement *cs_create_declaration_statement(CS_Creator *creator, ParsedType *type,
                                           char *name, Expression *initializer,
                                           bool is_static)
{
    Statement *stmt = cs_create_statement(creator, DECLARATION_STATEMENT);
    stmt->u.declaration_s = cs_create_declaration(creator, type, name, initializer, is_static);
    return stmt;
}

StatementList *cs_create_statement_list(Statement *stmt)
{
    StatementList *stmt_list = (StatementList *)calloc(1, sizeof(StatementList));
    stmt_list->stmt = stmt;
    stmt_list->next = NULL;
    return stmt_list;
}

DeclarationList *cs_create_declaration_list(Declaration *decl)
{
    DeclarationList *list = calloc(1, sizeof(DeclarationList));
    list->next = NULL;
    list->decl = decl;
    return list;
}

FunctionDeclaration *cs_create_function_declaration(CS_Creator *creator,
                                                    ParsedType *type,
                                                    char *name,
                                                    ParameterList *param,
                                                    bool is_variadic,
                                                    bool is_static,
                                                    AttributeSpecifier *attributes,
                                                    Statement *body)
{
    FunctionDeclaration *decl =
        (FunctionDeclaration *)calloc(1, sizeof(FunctionDeclaration));
    decl->type = NULL;
    decl->parsed_type = cs_copy_parsed_type(type);
    decl->name = name;
    decl->param = param;
    decl->is_variadic = is_variadic;
    decl->is_static = is_static;
    decl->attributes = attributes;
    decl->body = body;
    const char *path = creator ? creator->source_path : NULL;
    decl->source_path = path ? strdup(path) : NULL;
    decl->class_name = NULL; /* Set by header_decl_add_function from FileDecl */
    decl->index = -1;
    return decl;
}

ArgumentList *cs_create_argument(Expression *expr)
{
    ArgumentList *argument = calloc(1, sizeof(ArgumentList));
    argument->expr = expr;
    argument->next = NULL;
    return argument;
}

Expression *cs_create_initializer_list_expression(CS_Creator *creator,
                                                  ExpressionList *list)
{
    Expression *expr = cs_create_expression(creator, INITIALIZER_LIST_EXPRESSION);
    expr->u.initializer_list = list;
    return expr;
}

Expression *cs_create_designated_initializer_expression(CS_Creator *creator,
                                                        char *field_name,
                                                        Expression *value)
{
    Expression *expr = cs_create_expression(creator, DESIGNATED_INITIALIZER_EXPRESSION);
    expr->u.designated_initializer.field_name = field_name;
    expr->u.designated_initializer.value = value;
    return expr;
}

DeclarationList *cs_chain_declaration(DeclarationList *decl_list, Declaration *decl)
{
    DeclarationList *p;
    DeclarationList *list = cs_create_declaration_list(decl);
    if (decl_list == NULL)
        return list;
    for (p = decl_list; p->next; p = p->next)
        ;
    p->next = list;
    return decl_list;
}

StatementList *cs_chain_statement_list(StatementList *stmt_list,
                                       Statement *stmt)
{
    StatementList *p = NULL;
    StatementList *nstmt_list = cs_create_statement_list(stmt);
    if (stmt_list == NULL)
    {
        return nstmt_list;
    }
    for (p = stmt_list; p->next; p = p->next)
        ;
    p->next = nstmt_list;

    return stmt_list;
}

ParameterList *cs_chain_parameter_list(ParameterList *list, ParameterList *param)
{
    ParameterList *p = NULL;
    ParameterList *current = param;
    for (p = list; p->next; p = p->next)
        ;
    p->next = current;
    return list;
}

ArgumentList *cs_chain_argument_list(ArgumentList *list, Expression *expr)
{
    ArgumentList *p;
    ArgumentList *current = cs_create_argument(expr);
    for (p = list; p->next; p = p->next)
        ;
    p->next = current;
    return list;
}
