
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "meanvisitor.h"
#include "util.h"
#include "create.h"
#include "cminor_type.h"
#include "parsed_type.h"
#include "header_store.h"
#include "header_index.h"

static int mean_debug = 0;
#define DBG_PRINT(...) \
    if (mean_debug)    \
    fprintf(stderr, __VA_ARGS__)

char *get_type_name(CS_BasicType type)
{
    switch (type)
    {
    case CS_BOOLEAN_TYPE:
    {
        return "boolean";
    }
    case CS_VOID_TYPE:
    {
        return "void";
    }
    case CS_CHAR_TYPE:
    {
        return "char";
    }
    case CS_INT_TYPE:
    {
        return "int";
    }
    case CS_LONG_TYPE:
    {
        return "long";
    }
    case CS_FLOAT_TYPE:
    {
        return "float";
    }
    case CS_DOUBLE_TYPE:
    {
        return "double";
    }
    case CS_STRUCT_TYPE:
    {
        return "struct";
    }
    case CS_UNION_TYPE:
    {
        return "union";
    }
    case CS_ENUM_TYPE:
    {
        return "enum";
    }
    default:
    {
        return "untyped";
    }
    }
}

static void describe_type(TypeSpecifier *type, char *buffer, int buffer_size)
{
    if (!buffer || buffer_size == 0)
    {
        return;
    }
    cs_type_to_string(type, buffer, buffer_size);
}

static bool is_void_pointer(TypeSpecifier *type)
{
    if (!cs_type_is_pointer(type))
    {
        return false;
    }
    /* Check only one level: void* is true, void** is false */
    if (!cs_type_child(type))
    {
        return false;
    }
    return cs_type_is_void(cs_type_child(type));
}

/* Assign type to expression.
 * Type should already be resolved via cs_resolve_type(). */
static void assign_expression_type(Expression *expr, TypeSpecifier *type,
                                   CS_Compiler *compiler)
{
    (void)compiler;
    if (!expr)
    {
        return;
    }
    if (!type)
    {
        return;
    }
    /* Type is already resolved, just copy */
    expr->type = cs_copy_type_specifier(type);
}

static TypeSpecifier *resolve_parsed_type(CS_Compiler *compiler, const ParsedType *parsed)
{
    if (!parsed)
    {
        return NULL;
    }

    /* Use cs_resolve_type() which handles all type resolution */
    TypeSpecifier *resolved = cs_resolve_type(parsed,
                                              compiler ? compiler->header_store : NULL,
                                              compiler);
    if (!resolved && parsed->name)
    {
        /* Report error for unresolved named types */
        CS_BasicType basic = parsed->basic_type;
        if (basic == CS_ENUM_TYPE)
        {
            fprintf(stderr, "error: unknown enum '%s'\n", parsed->name);
        }
        else if (basic == CS_STRUCT_TYPE)
        {
            fprintf(stderr, "error: unknown struct '%s'\n", parsed->name);
        }
        else if (basic == CS_UNION_TYPE)
        {
            fprintf(stderr, "error: unknown union '%s'\n", parsed->name);
        }
        else
        {
            fprintf(stderr, "error: unknown type '%s'\n", parsed->name);
        }
    }
    return resolved;
}

static void finalize_declaration_type(Declaration *decl, CS_Compiler *compiler)
{
    if (!decl)
    {
        return;
    }
    if (decl->parsed_type)
    {
        decl->type = resolve_parsed_type(compiler, decl->parsed_type);
        return;
    }
    if (decl->type)
    {
        /* Type is already resolved, just copy */
        decl->type = cs_copy_type_specifier(decl->type);
    }
}

static TypeSpecifier *resolve_declaration_type(Declaration *decl, CS_Compiler *compiler)
{
    if (!decl)
    {
        return NULL;
    }
    if (decl->parsed_type)
    {
        decl->type = resolve_parsed_type(compiler, decl->parsed_type);
        return decl->type;
    }
    if (decl->type)
    {
        /* Type is already resolved, just copy */
        decl->type = cs_copy_type_specifier(decl->type);
    }
    return decl->type;
}

static void format_expr_error(char *buffer, int size, const Expression *expr, const char *fmt, ...)
{
    if (!buffer || size == 0)
    {
        return;
    }

    buffer[0] = '\0';
    int offset = 0;
    const char *path = NULL;
    int line = -1;
    if (expr)
    {
        path = expr->input_location.path;
        line = expr->input_location.line > 0 ? expr->input_location.line : expr->line_number;
    }

    if (path && line > 0)
    {
        int written = snprintf(buffer, size, "%s:%d: ", path, line);
        if (written > 0)
        {
            offset = (int)written < size ? (int)written : size - 1;
        }
    }
    else if (line > 0)
    {
        int written = snprintf(buffer, size, "line %d: ", line);
        if (written > 0)
        {
            offset = (int)written < size ? (int)written : size - 1;
        }
    }

    va_list ap;
    va_start(ap);
    if (offset < size)
    {
        vsnprintf(buffer + offset, size - offset, fmt, ap);
    }
    va_end(ap);
}

static void add_check_log(const char *str, Visitor *visitor)
{
    MeanVisitor *mean_visitor = (MeanVisitor *)visitor;
    const int LOG_LIMIT = 200;

    if (mean_visitor->log_count >= LOG_LIMIT)
    {
        return;
    }

    DBG_PRINT("add_check_log: %s\n", str);
    MeanCheckLogger *log = (MeanCheckLogger *)calloc(1, sizeof(MeanCheckLogger));
    if (!log)
    {
        DBG_PRINT("add_check_log: calloc failed for log\n");
        exit(1);
    }
    log->next = NULL;
    log->log_str = strdup(str);

    if (mean_visitor->check_log == NULL)
    {
        mean_visitor->check_log = log;
        mean_visitor->check_log_tail = log;
    }
    else
    {
        mean_visitor->check_log_tail->next = log;
        mean_visitor->check_log_tail = log;
    }

    mean_visitor->log_count++;
}

void show_mean_error(MeanVisitor *visitor)
{
    MeanCheckLogger *p;
    for (p = visitor->check_log; p; p = p->next)
    {
        fprintf(stderr, "%s\n", p->log_str);
    }
}

static void enter_castexpr(Expression *expr, Visitor *visitor) {}
static void leave_castexpr(Expression *expr, Visitor *visitor) {}

static void enter_typecastexpr(Expression *expr, Visitor *visitor) {}
static void leave_typecastexpr(Expression *expr, Visitor *visitor)
{
    DBG_PRINT("DEBUG: leave_typecastexpr\n");
    MeanVisitor *mean = (MeanVisitor *)visitor;
    /* Resolve cast target type (may contain typedef like uint16_t) */
    if (expr->parsed_type)
    {
        TypeSpecifier *resolved_type = resolve_parsed_type(mean->compiler, expr->parsed_type);
        assign_expression_type(expr, resolved_type, mean->compiler);
        expr->u.type_cast_expression.type = expr->type;
    }

    /* Check if casting integer 0 to pointer type */
    Expression *inner = expr->u.type_cast_expression.expr;
    TypeSpecifier *target_type = expr->type;

    if (target_type && cs_type_is_pointer(target_type) && inner && inner->type)
    {
        if (cs_type_is_integral(inner->type))
        {
            if (inner->kind == INT_EXPRESSION && inner->u.int_value == 0)
            {
                char message[256];
                char target_type_str[CS_TYPE_STRING_MAX];
                describe_type(target_type, target_type_str, sizeof target_type_str);
                format_expr_error(message, sizeof message, expr,
                                  "cannot cast integer 0 to pointer type %s; use NULL instead",
                                  target_type_str);
                add_check_log(message, (Visitor *)mean);
            }
            else if (inner->kind == INT_EXPRESSION || inner->kind == UINT_EXPRESSION ||
                     inner->kind == LONG_EXPRESSION || inner->kind == ULONG_EXPRESSION)
            {
                char message[256];
                char target_type_str[CS_TYPE_STRING_MAX];
                describe_type(target_type, target_type_str, sizeof target_type_str);
                format_expr_error(message, sizeof message, expr,
                                  "cannot cast integer to pointer type %s",
                                  target_type_str);
                add_check_log(message, (Visitor *)mean);
            }
        }
    }
}

static void enter_intexpr(Expression *expr, Visitor *visitor) {}
static void leave_intexpr(Expression *expr, Visitor *visitor)
{
    expr->type = cs_create_type_specifier(CS_INT_TYPE);
}

static void enter_uintexpr(Expression *expr, Visitor *visitor) {}
static void leave_uintexpr(Expression *expr, Visitor *visitor)
{
    TypeSpecifier *ts = cs_create_type_specifier(CS_INT_TYPE);
    cs_type_set_unsigned(ts, true);
    expr->type = ts;
}

static void enter_longexpr(Expression *expr, Visitor *visitor) {}
static void leave_longexpr(Expression *expr, Visitor *visitor)
{
    expr->type = cs_create_type_specifier(CS_LONG_TYPE);
}

static void enter_ulongexpr(Expression *expr, Visitor *visitor) {}
static void leave_ulongexpr(Expression *expr, Visitor *visitor)
{
    TypeSpecifier *ts = cs_create_type_specifier(CS_LONG_TYPE);
    cs_type_set_unsigned(ts, true);
    expr->type = ts;
}

static void enter_boolexpr(Expression *expr, Visitor *visitor) {}
static void leave_boolexpr(Expression *expr, Visitor *visitor)
{
    expr->type = cs_create_type_specifier(CS_BOOLEAN_TYPE);
}

static void enter_nullexpr(Expression *expr, Visitor *visitor) {}
static void leave_nullexpr(Expression *expr, Visitor *visitor)
{
    /* NULL has type void* - compatible with any pointer type */
    TypeSpecifier *void_type = cs_create_type_specifier(CS_VOID_TYPE);
    expr->type = cs_wrap_pointer(void_type, 1);
}

static void enter_stringexpr(Expression *expr, Visitor *visitor) {}
static void leave_stringexpr(Expression *expr, Visitor *visitor)
{
    TypeSpecifier *char_type = cs_create_type_specifier(CS_CHAR_TYPE);
    expr->type = cs_wrap_pointer(char_type, 1);
}

static void enter_doubleexpr(Expression *expr, Visitor *visitor) {}
static void leave_doubleexpr(Expression *expr, Visitor *visitor)
{
    expr->type = cs_create_type_specifier(CS_DOUBLE_TYPE);
}

static void enter_floatexpr(Expression *expr, Visitor *visitor) {}
static void leave_floatexpr(Expression *expr, Visitor *visitor)
{
    expr->type = cs_create_type_specifier(CS_FLOAT_TYPE);
}

static void push_scope(MeanVisitor *visitor)
{
    Scope *scope = (Scope *)calloc(1, sizeof(Scope));
    scope->decl_list = NULL;
    scope->next = visitor->current_scope;
    visitor->current_scope = scope;
}

static void pop_scope(MeanVisitor *visitor)
{
    if (visitor->current_scope)
    {
        visitor->current_scope = visitor->current_scope->next;
    }
}

static void add_decl_to_scope(MeanVisitor *visitor, Declaration *decl)
{
    if (visitor->current_scope)
    {
        DeclarationList *node = (DeclarationList *)calloc(1, sizeof(DeclarationList));
        node->decl = decl;
        node->next = visitor->current_scope->decl_list;
        visitor->current_scope->decl_list = node;
    }
}

static Declaration *search_decl_in_scope(MeanVisitor *visitor, char *name)
{
    for (Scope *scope = visitor->current_scope; scope; scope = scope->next)
    {
        for (DeclarationList *list = scope->decl_list; list; list = list->next)
        {
            if (!strcmp(list->decl->name, name))
            {
                return list->decl;
            }
        }
    }
    return NULL;
}

static void enter_identexpr(Expression *expr, Visitor *visitor)
{
    DBG_PRINT("DEBUG: enter_identexpr %s\n", expr->u.identifier.name);
}
static void leave_identexpr(Expression *expr, Visitor *visitor)
{
    // DBG_PRINT("DEBUG: leave_identexpr name=%s\n", expr->u.identifier.name);
    MeanVisitor *mean = (MeanVisitor *)visitor;
    if (expr->u.identifier.name == NULL)
    {
        DBG_PRINT("DEBUG: identifier name is NULL at line %d\n", expr->line_number);
        return;
    }
    /* Skip if already marked as function */
    if (expr->u.identifier.is_function)
    {
        return;
    }
    Declaration *decl = search_decl_in_scope(mean, expr->u.identifier.name);
    FunctionDeclaration *function = NULL;
    if (!decl)
    {
        // DBG_PRINT("DEBUG: searching global for %s\n", expr->u.identifier.name);
        // fflush(stderr);
        decl = cs_search_decl_global(mean->compiler, expr->u.identifier.name);
        // DBG_PRINT("DEBUG: global search done for %s, result=%p\n", expr->u.identifier.name, decl);
        // fflush(stderr);
    }

    if (decl)
    {
        TypeSpecifier *resolved = resolve_declaration_type(decl, mean->compiler);
        assign_expression_type(expr, resolved, mean->compiler);
        expr->u.identifier.u.declaration = decl;
        expr->u.identifier.is_function = false;
        return;
    }
    // DBG_PRINT("DEBUG: searching function for %s\n", expr->u.identifier.name);
    function = cs_search_function(mean->compiler, expr->u.identifier.name);
    if (function)
    {
        assign_expression_type(expr, function->type, mean->compiler);
        expr->u.identifier.u.function = function;
        expr->u.identifier.is_function = true;
        return;
    }

    EnumMember *enum_member = cs_lookup_enum_member(mean->compiler, expr->u.identifier.name);
    if (enum_member)
    {
        /* Set type to the enum type (named enum) */
        if (enum_member->enum_def && enum_member->enum_def->id.name)
        {
            expr->type = cs_create_named_type_specifier(CS_ENUM_TYPE, enum_member->enum_def->id.name);
        }
        else
        {
            /* Anonymous enum: treat as int */
            expr->type = cs_create_type_specifier(CS_INT_TYPE);
        }
        expr->u.identifier.u.enum_member = enum_member;
        expr->u.identifier.is_function = false;
        expr->u.identifier.is_enum_member = true;
        return;
    }

    char message[256];
    format_expr_error(message, sizeof message, expr,
                      "Cannot find identifier %s", expr->u.identifier.name);
    add_check_log(message, visitor);
    expr->type = cs_create_type_specifier(CS_INT_TYPE);
}

static bool check_nulltype_binary_expr(Expression *expr,
                                       Visitor *visitor)
{
    Expression *left = expr->u.binary_expression.left;
    Expression *right = expr->u.binary_expression.right;
    if (left->type == NULL)
    {
        char message[256];
        format_expr_error(message, sizeof message, expr,
                          "Cannot find left hand type");
        add_check_log(message, visitor);
    }
    if (right->type == NULL)
    {
        char message[256];
        format_expr_error(message, sizeof message, expr,
                          "Cannot find right hand type");
        add_check_log(message, visitor);
    }
    if ((left->type == NULL) || (right->type == NULL))
    {
        return true;
    }
    return false;
}

static void unacceptable_type_binary_expr(Expression *expr, Visitor *visitor)
{
    Expression *left = expr->u.binary_expression.left;
    Expression *right = expr->u.binary_expression.right;
    char message[256];
    char left_type[CS_TYPE_STRING_MAX];
    char right_type[CS_TYPE_STRING_MAX];
    describe_type(left->type, left_type, sizeof left_type);
    describe_type(right->type, right_type, sizeof right_type);
    format_expr_error(
        message, sizeof message, expr,
        "type mismatch in arithmetic binary expression left:%s, right:%s",
        left_type,
        right_type);

    add_check_log(message, visitor);
}

/* Create a temporary creator from an existing expression's location */
static CS_Creator creator_from_expr(Expression *expr)
{
    CS_Creator c = {};
    if (expr)
    {
        c.line_number = expr->line_number;
        c.source_path = expr->input_location.path;
    }
    return c;
}

/* Helper: Insert cast expression for widening conversion */
static Expression *insert_widening_to_type(Expression *operand, TypeSpecifier *target,
                                           CS_CastType ctype)
{
    CS_Creator c = creator_from_expr(operand);
    Expression *cast = cs_create_cast_expression(&c, ctype, operand);
    cast->type = cs_copy_type_specifier(target);
    cast->u.cast_expression.expr = operand;
    return cast;
}

/* Java Numeric Type Promotion for Binary Operations:
 * 1. If either operand is double, convert the other to double
 * 2. Otherwise, if either operand is float, convert the other to float
 * 3. Otherwise, if either operand is long, convert the other to long
 * 4. Otherwise, both operands are converted to int
 */
static void cast_arithmetic_binary_expr(Expression *expr, Visitor *visitor)
{
    Expression *left = expr->u.binary_expression.left;
    Expression *right = expr->u.binary_expression.right;

    if (check_nulltype_binary_expr(expr, visitor))
    {
        return;
    }

    bool left_ptr = cs_type_is_pointer(left->type);
    bool right_ptr = cs_type_is_pointer(right->type);
    bool left_numeric = cs_type_is_numeric(left->type);
    bool right_numeric = cs_type_is_numeric(right->type);

    /* Pointer arithmetic: ptr + int, int + ptr, ptr - ptr */
    if ((expr->kind == ADD_EXPRESSION || expr->kind == SUB_EXPRESSION) &&
        left_ptr && right_numeric)
    {
        expr->type = cs_copy_type_specifier(left->type);
        return;
    }
    if (expr->kind == ADD_EXPRESSION && right_ptr && left_numeric)
    {
        expr->type = cs_copy_type_specifier(right->type);
        return;
    }
    if (expr->kind == SUB_EXPRESSION && left_ptr && right_ptr)
    {
        expr->type = cs_create_type_specifier(CS_INT_TYPE);
        return;
    }

    /* Both operands must be numeric (or enum treated as int) */
    bool left_enum = cs_type_is_enum(left->type);
    bool right_enum = cs_type_is_enum(right->type);

    if (!left_numeric || !right_numeric)
    {
        /* Enum types: allow enum <-> int arithmetic, block different enums */
        if (left_enum || right_enum)
        {
            /* Both enums - different enum types cannot be mixed */
            if (left_enum && right_enum && !cs_type_equals(left->type, right->type))
            {
                unacceptable_type_binary_expr(expr, visitor);
                return;
            }
            /* enum <-> int or same enum: OK, treat as int arithmetic */
            if ((left_enum && (right_enum || cs_type_is_integral(right->type))) ||
                (right_enum && cs_type_is_integral(left->type)))
            {
                expr->type = cs_create_type_specifier(CS_INT_TYPE);
                return;
            }
        }
        unacceptable_type_binary_expr(expr, visitor);
        return;
    }

    /* Arithmetic operations allow mixed signedness.
     * Two's complement representation means the bit operations are identical.
     * Result type: unsigned if either operand is unsigned. */

    /* Determine result type using binary numeric promotion */
    TypeSpecifier *result_type = cs_type_binary_promoted_specifier(left->type,
                                                                   right->type);
    if (!result_type)
    {
        unacceptable_type_binary_expr(expr, visitor);
        return;
    }

    /* Insert casts if needed */
    CS_CastType left_cast = cs_type_widening_cast_to(left->type, result_type);
    CS_CastType right_cast = cs_type_widening_cast_to(right->type, result_type);

    if (left_cast != 0)
    {
        expr->u.binary_expression.left =
            insert_widening_to_type(left, result_type, left_cast);
    }
    if (right_cast != 0)
    {
        expr->u.binary_expression.right =
            insert_widening_to_type(right, result_type, right_cast);
    }

    expr->type = result_type;
}

/* arithmetic calculation*/
static void enter_addexpr(Expression *expr, Visitor *visitor) {}
static void leave_addexpr(Expression *expr, Visitor *visitor)
{
    cast_arithmetic_binary_expr(expr, visitor);
}
static void enter_subexpr(Expression *expr, Visitor *visitor) {}
static void leave_subexpr(Expression *expr, Visitor *visitor)
{
    cast_arithmetic_binary_expr(expr, visitor);
}

static void enter_mulexpr(Expression *expr, Visitor *visitor) {}
static void leave_mulexpr(Expression *expr, Visitor *visitor)
{
    cast_arithmetic_binary_expr(expr, visitor);
}

/* Division and modulo require matching signedness.
 * JVM uses different instructions: idiv/ldiv for signed,
 * Integer.divideUnsigned/Long.divideUnsigned for unsigned. */
static void cast_division_binary_expr(Expression *expr, Visitor *visitor)
{
    if (check_nulltype_binary_expr(expr, visitor))
    {
        return;
    }
    Expression *left = expr->u.binary_expression.left;
    Expression *right = expr->u.binary_expression.right;

    /* Both operands must be numeric */
    bool left_numeric = cs_type_is_numeric(left->type);
    bool right_numeric = cs_type_is_numeric(right->type);

    if (!left_numeric || !right_numeric)
    {
        unacceptable_type_binary_expr(expr, visitor);
        return;
    }

    /* Division requires matching signedness for integral types */
    if (!cs_type_can_mix_for_division(left->type, right->type))
    {
        char message[256];
        char left_type[CS_TYPE_STRING_MAX];
        char right_type[CS_TYPE_STRING_MAX];
        describe_type(left->type, left_type, sizeof left_type);
        describe_type(right->type, right_type, sizeof right_type);
        format_expr_error(
            message, sizeof message, expr,
            "signed/unsigned mismatch in division: cannot divide %s by %s; use explicit cast",
            left_type, right_type);
        add_check_log(message, visitor);
        return;
    }

    /* Determine result type using binary numeric promotion */
    TypeSpecifier *result_type = cs_type_binary_promoted_specifier(left->type,
                                                                   right->type);
    if (!result_type)
    {
        unacceptable_type_binary_expr(expr, visitor);
        return;
    }

    /* Insert casts if needed */
    CS_CastType left_cast = cs_type_widening_cast_to(left->type, result_type);
    CS_CastType right_cast = cs_type_widening_cast_to(right->type, result_type);

    if (left_cast != 0)
    {
        expr->u.binary_expression.left =
            insert_widening_to_type(left, result_type, left_cast);
    }
    if (right_cast != 0)
    {
        expr->u.binary_expression.right =
            insert_widening_to_type(right, result_type, right_cast);
    }

    expr->type = result_type;
}

static void enter_divexpr(Expression *expr, Visitor *visitor) {}
static void leave_divexpr(Expression *expr, Visitor *visitor)
{
    cast_division_binary_expr(expr, visitor);
}
static void enter_modexpr(Expression *expr, Visitor *visitor) {}
static void leave_modexpr(Expression *expr, Visitor *visitor)
{
    cast_division_binary_expr(expr, visitor);
}

/* Bitwise operation type promotion (&, |, ^):
 * - Both operands must be integral types
 * - Mixed signedness is allowed (two's complement same bit pattern)
 * - Signed wins: result is unsigned only if both operands are unsigned
 */
static void bitwise_type_check(Expression *expr, Visitor *visitor)
{
    Expression *left = expr->u.binary_expression.left;
    Expression *right = expr->u.binary_expression.right;

    if (check_nulltype_binary_expr(expr, visitor))
    {
        return;
    }

    /* Handle enum types: allow enum <-> int and same-enum bitwise operations */
    bool left_enum = cs_type_is_enum(left->type);
    bool right_enum = cs_type_is_enum(right->type);

    if (left_enum || right_enum)
    {
        /* Both enums - different enum types cannot be mixed */
        if (left_enum && right_enum && !cs_type_equals(left->type, right->type))
        {
            unacceptable_type_binary_expr(expr, visitor);
            return;
        }
        /* enum <-> int or same enum: OK, treat as int */
        if ((left_enum && (right_enum || cs_type_is_integral(right->type))) ||
            (right_enum && cs_type_is_integral(left->type)))
        {
            expr->type = cs_create_type_specifier(CS_INT_TYPE);
            return;
        }
    }

    /* Both operands must be integral (char/short/int/long) */
    if (!cs_type_is_integral(left->type) || !cs_type_is_integral(right->type))
    {
        char message[256];
        char left_type[CS_TYPE_STRING_MAX];
        char right_type[CS_TYPE_STRING_MAX];
        describe_type(left->type, left_type, sizeof left_type);
        describe_type(right->type, right_type, sizeof right_type);
        format_expr_error(
            message, sizeof message, expr,
            "bitwise operations require integral types, got left:%s, right:%s",
            left_type,
            right_type);
        add_check_log(message, visitor);
        return;
    }

    /* Bitwise operations allow mixed signedness - use standard binary promotion */
    TypeSpecifier *result_type = cs_type_binary_promoted_specifier(left->type, right->type);
    if (!result_type)
    {
        unacceptable_type_binary_expr(expr, visitor);
        return;
    }

    /* Insert widening casts if needed */
    CS_CastType left_cast = cs_type_widening_cast_to(left->type, result_type);
    CS_CastType right_cast = cs_type_widening_cast_to(right->type, result_type);

    if (left_cast != 0)
    {
        expr->u.binary_expression.left =
            insert_widening_to_type(left, result_type, left_cast);
    }
    if (right_cast != 0)
    {
        expr->u.binary_expression.right =
            insert_widening_to_type(right, result_type, right_cast);
    }

    expr->type = result_type;
}

static void enter_bitandexpr(Expression *expr, Visitor *visitor) {}
static void leave_bitandexpr(Expression *expr, Visitor *visitor)
{
    bitwise_type_check(expr, visitor);
}

static void enter_bitxorexpr(Expression *expr, Visitor *visitor) {}
static void leave_bitxorexpr(Expression *expr, Visitor *visitor)
{
    bitwise_type_check(expr, visitor);
}

static void enter_bitorexpr(Expression *expr, Visitor *visitor) {}
static void leave_bitorexpr(Expression *expr, Visitor *visitor)
{
    bitwise_type_check(expr, visitor);
}

/* Java shift operation type promotion (<<, >>, >>>):
 * - Left operand (value to shift) is promoted: byte/short/char -> int
 * - Right operand (shift amount) must be integral, but is NOT converted to match left
 * - Result type is the promoted type of the left operand (int or long)
 */
static void shift_type_check(Expression *expr, Visitor *visitor)
{
    Expression *left = expr->u.binary_expression.left;
    Expression *right = expr->u.binary_expression.right;

    if (check_nulltype_binary_expr(expr, visitor))
    {
        return;
    }

    /* Both operands must be integral */
    if (!cs_type_is_integral(left->type) || !cs_type_is_integral(right->type))
    {
        char message[256];
        char left_type[CS_TYPE_STRING_MAX];
        char right_type[CS_TYPE_STRING_MAX];
        describe_type(left->type, left_type, sizeof left_type);
        describe_type(right->type, right_type, sizeof right_type);
        format_expr_error(
            message, sizeof message, expr,
            "shift operations require integral types, got left:%s, right:%s",
            left_type,
            right_type);
        add_check_log(message, visitor);
        return;
    }

    /* Determine result type based on left operand:
     * - If left is long, result is long
     * - Otherwise (byte/short/char/int), result is int */
    TypeSpecifier *result_type = cs_type_is_long_exact(left->type)
                                     ? cs_create_type_specifier(CS_LONG_TYPE)
                                     : cs_create_type_specifier(CS_INT_TYPE);
    /* Propagate unsigned flag from left operand */
    if (cs_type_is_unsigned(left->type))
    {
        cs_type_set_unsigned(result_type, true);
    }

    /* Only promote left operand if needed */
    CS_CastType left_cast = cs_type_widening_cast_to(left->type, result_type);
    if (left_cast != 0)
    {
        expr->u.binary_expression.left =
            insert_widening_to_type(left, result_type, left_cast);
    }

    /* Right operand is NOT promoted to match left - it stays as int
     * (Java uses only the low 5 bits for int shift, low 6 bits for long shift) */

    expr->type = result_type;
}

static void enter_lshiftexpr(Expression *expr, Visitor *visitor) {}
static void leave_lshiftexpr(Expression *expr, Visitor *visitor)
{
    shift_type_check(expr, visitor);
}

static void enter_rshiftexpr(Expression *expr, Visitor *visitor) {}
static void leave_rshiftexpr(Expression *expr, Visitor *visitor)
{
    shift_type_check(expr, visitor);
}

/* Java comparison operation type promotion (<, >, <=, >=, ==, !=):
 * - Same rules as binary numeric promotion for arithmetic
 * - Pointers must match exactly
 * - Result type is always boolean
 */
static void compare_type_check(Expression *expr, Visitor *visitor)
{
    Expression *left = expr->u.binary_expression.left;
    Expression *right = expr->u.binary_expression.right;

    if (check_nulltype_binary_expr(expr, visitor))
    {
        return;
    }

    /* Pointer comparison: types must match exactly */
    bool left_ptr = cs_type_is_pointer(left->type);
    bool right_ptr = cs_type_is_pointer(right->type);
    if (left_ptr || right_ptr)
    {
        if (cs_type_equals(left->type, right->type))
        {
            MeanVisitor *mean = (MeanVisitor *)visitor;
            assign_expression_type(expr, left->type, mean->compiler);
            return;
        }
        unacceptable_type_binary_expr(expr, visitor);
        return;
    }

    /* Numeric comparison: apply binary numeric promotion */
    bool left_numeric = cs_type_is_numeric(left->type);
    bool right_numeric = cs_type_is_numeric(right->type);

    if (!left_numeric || !right_numeric)
    {
        bool left_enum = cs_type_is_enum(left->type);
        bool right_enum = cs_type_is_enum(right->type);

        /* Enum types: allow enum <-> int, block different enums */
        if (left_enum || right_enum)
        {
            /* Both enums - different enum types cannot be compared */
            if (left_enum && right_enum && !cs_type_equals(left->type, right->type))
            {
                char message[256];
                char left_type_str[CS_TYPE_STRING_MAX];
                char right_type_str[CS_TYPE_STRING_MAX];
                describe_type(left->type, left_type_str, sizeof left_type_str);
                describe_type(right->type, right_type_str, sizeof right_type_str);
                format_expr_error(message, sizeof message, expr,
                                  "cannot compare different enum types %s and %s",
                                  left_type_str, right_type_str);
                add_check_log(message, visitor);
                return;
            }
            /* enum <-> int or same enum: OK, enum is treated as int on JVM */
            if ((left_enum && (right_enum || cs_type_is_integral(right->type))) ||
                (right_enum && cs_type_is_integral(left->type)))
            {
                /* Result is boolean, set by caller */
                return;
            }
            /* enum with non-integral type */
            char message[256];
            format_expr_error(message, sizeof message, expr,
                              "relational comparison not allowed for enum with non-integral type");
            add_check_log(message, visitor);
            return;
        }
        /* Other non-numeric types: require exact match (e.g., pointers already handled) */
        if (cs_type_equals(left->type, right->type))
        {
            MeanVisitor *mean = (MeanVisitor *)visitor;
            assign_expression_type(expr, left->type, mean->compiler);
            return;
        }
        unacceptable_type_binary_expr(expr, visitor);
        return;
    }

    /* Comparison operations prohibit mixed signedness.
     * Signed and unsigned comparisons have different semantics:
     * e.g., (int)-1 < (int)0 but (uint)-1 > (uint)0 */
    if (!cs_type_can_mix_for_comparison(left->type, right->type))
    {
        char message[256];
        char left_type[CS_TYPE_STRING_MAX];
        char right_type[CS_TYPE_STRING_MAX];
        describe_type(left->type, left_type, sizeof left_type);
        describe_type(right->type, right_type, sizeof right_type);
        format_expr_error(
            message, sizeof message, expr,
            "signed/unsigned mismatch: cannot compare %s and %s; use explicit cast",
            left_type, right_type);
        add_check_log(message, visitor);
        return;
    }

    /* Determine common type using binary numeric promotion */
    TypeSpecifier *common_type = cs_type_binary_promoted_specifier(left->type,
                                                                   right->type);
    if (!common_type)
    {
        unacceptable_type_binary_expr(expr, visitor);
        return;
    }

    /* Insert casts if needed */
    CS_CastType left_cast = cs_type_widening_cast_to(left->type, common_type);
    CS_CastType right_cast = cs_type_widening_cast_to(right->type, common_type);

    if (left_cast != 0)
    {
        expr->u.binary_expression.left =
            insert_widening_to_type(left, common_type, left_cast);
    }
    if (right_cast != 0)
    {
        expr->u.binary_expression.right =
            insert_widening_to_type(right, common_type, right_cast);
    }

    /* Result type for comparisons will be set to boolean by the caller */
}

static void enter_gtexpr(Expression *expr, Visitor *visitor) {}
static void leave_gtexpr(Expression *expr, Visitor *visitor)
{
    compare_type_check(expr, visitor);
    expr->type = cs_create_type_specifier(CS_BOOLEAN_TYPE);
}

static void enter_geexpr(Expression *expr, Visitor *visitor) {}
static void leave_geexpr(Expression *expr, Visitor *visitor)
{
    compare_type_check(expr, visitor);
    expr->type = cs_create_type_specifier(CS_BOOLEAN_TYPE);
}

static void enter_ltexpr(Expression *expr, Visitor *visitor) {}
static void leave_ltexpr(Expression *expr, Visitor *visitor)
{
    compare_type_check(expr, visitor);
    expr->type = cs_create_type_specifier(CS_BOOLEAN_TYPE);
}

static void enter_leexpr(Expression *expr, Visitor *visitor) {}
static void leave_leexpr(Expression *expr, Visitor *visitor)
{
    compare_type_check(expr, visitor);
    expr->type = cs_create_type_specifier(CS_BOOLEAN_TYPE);
}

static void compare_equality_type_check(Expression *expr, Visitor *visitor)
{
    if (check_nulltype_binary_expr(expr, visitor))
    {
        return;
    }
    Expression *left = expr->u.binary_expression.left;
    Expression *right = expr->u.binary_expression.right;

    bool left_ptr = cs_type_is_pointer(left->type);
    bool right_ptr = cs_type_is_pointer(right->type);

    /* In Cminor, ptr == 0 is a type error. Use ptr == NULL instead.
     * This ensures consistent pointer representation (__intPtr, __charPtr, etc.) */

    /* Allow pointer-pointer comparisons (including void* for NULL) */
    if (left_ptr && right_ptr)
    {
        return;
    }

    /* For all other cases, require exact type match */
    if (cs_type_equals(left->type, right->type))
    {
        return;
    }

    /* Enum types: allow enum <-> int comparison, but not different enum types */
    bool left_enum = cs_type_is_enum(left->type);
    bool right_enum = cs_type_is_enum(right->type);
    if (left_enum || right_enum)
    {
        /* Both are enums - must be the same enum type (already checked by cs_type_equals above) */
        if (left_enum && right_enum)
        {
            char message[256];
            char left_type[CS_TYPE_STRING_MAX];
            char right_type[CS_TYPE_STRING_MAX];
            describe_type(left->type, left_type, sizeof left_type);
            describe_type(right->type, right_type, sizeof right_type);
            format_expr_error(
                message, sizeof message, expr,
                "cannot compare different enum types %s and %s",
                left_type, right_type);
            add_check_log(message, visitor);
            return;
        }
        /* enum <-> int comparison is allowed */
        if ((left_enum && cs_type_is_integral(right->type)) ||
            (cs_type_is_integral(left->type) && right_enum))
        {
            return;
        }
        /* enum with other non-integral types is not allowed */
        char message[256];
        char left_type[CS_TYPE_STRING_MAX];
        char right_type[CS_TYPE_STRING_MAX];
        describe_type(left->type, left_type, sizeof left_type);
        describe_type(right->type, right_type, sizeof right_type);
        format_expr_error(
            message, sizeof message, expr,
            "cannot compare enum %s with %s",
            left_enum ? left_type : right_type,
            left_enum ? right_type : left_type);
        add_check_log(message, visitor);
        return;
    }

    if (cs_type_is_bool(left->type) && !cs_type_is_bool(right->type))
    {
        char message[256];
        char left_type[CS_TYPE_STRING_MAX];
        char right_type[CS_TYPE_STRING_MAX];
        describe_type(left->type, left_type, sizeof left_type);
        describe_type(right->type, right_type, sizeof right_type);
        format_expr_error(
            message, sizeof message, expr,
            "type mismatch in equality binary expression left:%s, right:%s",
            left_type,
            right_type);
        add_check_log(message, visitor);
    }
    else if (!cs_type_is_bool(left->type) && cs_type_is_bool(right->type))
    {
        char message[256];
        char left_type[CS_TYPE_STRING_MAX];
        char right_type[CS_TYPE_STRING_MAX];
        describe_type(left->type, left_type, sizeof left_type);
        describe_type(right->type, right_type, sizeof right_type);
        format_expr_error(
            message, sizeof message, expr,
            "type mismatch in equality binary expression left:%s, right:%s",
            left_type,
            right_type);
        add_check_log(message, visitor);
    }
    else
    {
        compare_type_check(expr, visitor);
    }
}

static void enter_eqexpr(Expression *expr, Visitor *visitor) {}
static void leave_eqexpr(Expression *expr, Visitor *visitor)
{
    compare_equality_type_check(expr, visitor);
    expr->type = cs_create_type_specifier(CS_BOOLEAN_TYPE);
}

static void enter_neexpr(Expression *expr, Visitor *visitor) {}
static void leave_neexpr(Expression *expr, Visitor *visitor)
{
    compare_equality_type_check(expr, visitor);
    expr->type = cs_create_type_specifier(CS_BOOLEAN_TYPE);
}

/* Check if type is valid for logical operations (&&, ||)
 * Accepts: bool, small integral (char/short/int), pointer
 * Does NOT accept: long, float/double (JVM ifeq is 32-bit only) */
static bool is_logical_operand_type(TypeSpecifier *type)
{
    if (cs_type_is_long_exact(type))
    {
        return false; /* long requires lcmp, not supported in logical ops */
    }
    return cs_type_is_bool(type) ||
           cs_type_is_integral(type) ||
           cs_type_is_pointer(type);
}

static void logical_type_check(Expression *expr, Visitor *visitor)
{
    if (check_nulltype_binary_expr(expr, visitor))
    {
        return;
    }

    Expression *left = expr->u.binary_expression.left;
    Expression *right = expr->u.binary_expression.right;

    /* Check for long type with specific error message */
    char message[256];
    if (cs_type_is_long_exact(left->type))
    {
        format_expr_error(message, sizeof message, expr,
                          "long type not allowed in && or ||; use explicit comparison (e.g., x != 0)");
        add_check_log(message, visitor);
        return;
    }
    if (cs_type_is_long_exact(right->type))
    {
        format_expr_error(message, sizeof message, expr,
                          "long type not allowed in && or ||; use explicit comparison (e.g., x != 0)");
        add_check_log(message, visitor);
        return;
    }

    if (is_logical_operand_type(left->type) && is_logical_operand_type(right->type))
    {
        expr->type = cs_create_type_specifier(CS_BOOLEAN_TYPE);
        return;
    }

    char left_type[CS_TYPE_STRING_MAX];
    char right_type[CS_TYPE_STRING_MAX];
    describe_type(left->type, left_type, sizeof left_type);
    describe_type(right->type, right_type, sizeof right_type);
    format_expr_error(
        message, sizeof message, expr,
        "&& or || require bool, int, or pointer operands, got left:%s, right:%s",
        left_type,
        right_type);
    add_check_log(message, visitor);
}

static void enter_landexpr(Expression *expr, Visitor *visitor) {}
static void leave_landexpr(Expression *expr, Visitor *visitor)
{
    logical_type_check(expr, visitor);
}

static void enter_lorexpr(Expression *expr, Visitor *visitor) {}
static void leave_lorexpr(Expression *expr, Visitor *visitor)
{
    logical_type_check(expr, visitor);
}

static void incdec_typecheck(Expression *expr, Visitor *visitor)
{
    Expression *idexpr = expr->u.inc_dec.target;
    char message[256];
    if (idexpr->type == NULL)
    {
        format_expr_error(message, sizeof message, expr,
                          "Cannot find ++ or -- type");
        add_check_log(message, visitor);
        return;
    }

    ExpressionKind eKind = expr->u.inc_dec.target->kind;
    if (eKind == INT_EXPRESSION || eKind == BOOL_EXPRESSION)
    {
        format_expr_error(message, sizeof message, expr,
                          "Operand is Immediate data)");
        add_check_log(message, visitor);
    }

    if (eKind == IDENTIFIER_EXPRESSION)
    {
        if (expr->u.inc_dec.target->u.identifier.is_function)
        {
            format_expr_error(message, sizeof message, expr,
                              "Variable should not be a function)");
        }
    }

    bool is_numeric = cs_type_is_integral(idexpr->type) || cs_type_is_floating(idexpr->type);
    bool is_pointer = cs_type_is_pointer(idexpr->type);
    if (!is_numeric && !is_pointer)
    {
        char type_desc[CS_TYPE_STRING_MAX];
        describe_type(idexpr->type, type_desc, sizeof type_desc);
        format_expr_error(message, sizeof message, expr,
                          "Operand is not INT/DOUBLE or pointer type (%s)",
                          type_desc);
        add_check_log(message, visitor);
        return;
    }

    expr->type = idexpr->type;
}
static void enter_incexpr(Expression *expr, Visitor *visitor) {}
static void leave_incexpr(Expression *expr, Visitor *visitor)
{
    incdec_typecheck(expr, visitor);
}

static void enter_decexpr(Expression *expr, Visitor *visitor) {}
static void leave_decexpr(Expression *expr, Visitor *visitor)
{
    incdec_typecheck(expr, visitor);
}

static void enter_minusexpr(Expression *expr, Visitor *visitor) {}
static void leave_minusexpr(Expression *expr, Visitor *visitor)
{
    char message[256];
    TypeSpecifier *type = expr->u.minus_expression->type;
    if (type == NULL)
    {
        format_expr_error(message, sizeof message, expr,
                          "Cannot find - type");
        add_check_log(message, visitor);
        return;
    }

    if (!cs_type_is_integral(type) &&
        !cs_type_is_floating(type))
    {
        char type_desc[CS_TYPE_STRING_MAX];
        describe_type(type, type_desc, sizeof type_desc);
        format_expr_error(message, sizeof message, expr,
                          "Operand is not INT or DOUBLE type (%s)",
                          type_desc);
        add_check_log(message, visitor);
        return;
    }

    /* Unary promotion: small_int -> int/uint based on signedness */
    expr->type = cs_type_unary_promoted(type);
}

static void enter_plusexpr(Expression *expr, Visitor *visitor) {}
static void leave_plusexpr(Expression *expr, Visitor *visitor)
{
    char message[256];
    TypeSpecifier *type = expr->u.plus_expression->type;
    if (type == NULL)
    {
        format_expr_error(message, sizeof message, expr,
                          "Cannot find + type");
        add_check_log(message, visitor);
        return;
    }

    if (!cs_type_is_integral(type) &&
        !cs_type_is_floating(type))
    {
        char type_desc[CS_TYPE_STRING_MAX];
        describe_type(type, type_desc, sizeof type_desc);
        format_expr_error(message, sizeof message, expr,
                          "Operand is not INT or DOUBLE type (%s)",
                          type_desc);
        add_check_log(message, visitor);
        return;
    }

    /* Unary promotion: small_int -> int/uint based on signedness */
    expr->type = cs_type_unary_promoted(type);
}

static void enter_lognotexpr(Expression *expr, Visitor *visitor) {}
static void leave_lognotexpr(Expression *expr, Visitor *visitor)
{
    char message[256];
    TypeSpecifier *type = expr->u.logical_not_expression->type;

    if (type == NULL)
    {
        format_expr_error(message, sizeof message, expr,
                          "Cannot find ! type");
        add_check_log(message, visitor);
        return;
    }

    /* ! accepts: bool, small integral (char/short/int), pointer */
    /* Does NOT accept: long, float/double (JVM ifeq is 32-bit only) */
    if (cs_type_is_long_exact(type))
    {
        format_expr_error(message, sizeof message, expr,
                          "long type not allowed in !; use explicit comparison (e.g., x != 0)");
        add_check_log(message, visitor);
        return;
    }

    if (!cs_type_is_bool(type) &&
        !cs_type_is_integral(type) &&
        !cs_type_is_pointer(type))
    {
        char type_desc[CS_TYPE_STRING_MAX];
        describe_type(type, type_desc, sizeof type_desc);
        format_expr_error(message, sizeof message, expr,
                          "Operand of ! must be bool, int, or pointer (%s)",
                          type_desc);
        add_check_log(message, visitor);
        return;
    }

    expr->type = cs_create_type_specifier(CS_BOOLEAN_TYPE);
}

static void enter_bitnotexpr(Expression *expr, Visitor *visitor) {}
static void leave_bitnotexpr(Expression *expr, Visitor *visitor)
{
    char message[256];
    TypeSpecifier *type = expr->u.bit_not_expression->type;

    if (type == NULL)
    {
        format_expr_error(message, sizeof message, expr,
                          "Cannot find ~ type");
        add_check_log(message, visitor);
        return;
    }

    if (!cs_type_is_integral(type))
    {
        char type_desc[CS_TYPE_STRING_MAX];
        describe_type(type, type_desc, sizeof type_desc);
        format_expr_error(message, sizeof message, expr,
                          "Operand is not integral type (%s)",
                          type_desc);
        add_check_log(message, visitor);
        return;
    }

    /* Unary promotion: small_int -> int/uint based on signedness */
    expr->type = cs_type_unary_promoted(type);
}

/* Check if expression type is compatible with target type for assignment.
 * If allow_narrowing is true, narrowing conversions are allowed (for compound assignments).
 * Java compound assignment (e.g., c += i) implicitly casts the result to the target type.
 */
static Expression *assignment_type_check(TypeSpecifier *ltype, Expression *expr,
                                         Visitor *visitor, bool allow_narrowing)
{
    DBG_PRINT("DEBUG: assignment_type_check ltype=%p expr->type=%p expr->kind=%d\n", ltype, expr->type, expr->kind);
    fflush(stderr);
    if (ltype == NULL)
    {
        char message[256];
        format_expr_error(message, sizeof message, expr,
                          "Cannot find left hand type");
        add_check_log(message, visitor);
        return expr;
    }
    if (expr->type == NULL)
    {
        if (expr->kind == INITIALIZER_LIST_EXPRESSION ||
            expr->kind == DESIGNATED_INITIALIZER_EXPRESSION)
        {
            expr->type = ltype;
            return expr;
        }
        char message[256];
        DBG_PRINT("DEBUG: expr->type is NULL\n");
        fflush(stderr);
        format_expr_error(message, sizeof message, expr,
                          "Cannot find right hand type");
        add_check_log(message, visitor);
        return expr;
    }

    if (cs_type_is_pointer(ltype))
    {
        if (cs_type_is_pointer(expr->type))
        {
            if (is_void_pointer(ltype) || is_void_pointer(expr->type))
            {
                /* If the right side is void* (includes NULL), propagate the
                 * target type for proper JVM bytecode generation.
                 * This ensures NULL gets the correct pointer wrapper type. */
                if (is_void_pointer(expr->type) && !is_void_pointer(ltype))
                {
                    expr->type = ltype;
                }
                return expr;
            }
            return expr;
        }
        /* int -> pointer assignment is prohibited in Cminor.
         * Use NULL (which has type void*) instead of integer constants. */

        /* Check if trying to assign integer 0 to pointer (use NULL instead) */
        if (cs_type_is_integral(expr->type))
        {
            if (expr->kind == INT_EXPRESSION && expr->u.int_value == 0)
            {
                char message[256];
                char lhs_type_str[CS_TYPE_STRING_MAX];
                describe_type(ltype, lhs_type_str, sizeof lhs_type_str);
                format_expr_error(message, sizeof message, expr,
                                  "cannot assign integer 0 to pointer type %s; use NULL instead",
                                  lhs_type_str);
                add_check_log(message, visitor);
                return expr;
            }
            else if (expr->kind == INT_EXPRESSION || expr->kind == UINT_EXPRESSION ||
                     expr->kind == LONG_EXPRESSION || expr->kind == ULONG_EXPRESSION)
            {
                char message[256];
                char lhs_type_str[CS_TYPE_STRING_MAX];
                describe_type(ltype, lhs_type_str, sizeof lhs_type_str);
                format_expr_error(message, sizeof message, expr,
                                  "cannot assign integer to pointer type %s",
                                  lhs_type_str);
                add_check_log(message, visitor);
                return expr;
            }
        }

        /* Array to pointer implicit conversion: T[] -> T* */
        if (cs_type_is_array(expr->type))
        {
            /* Get direct child element (1 level only, not deepest child) */
            TypeSpecifier *array_elem = cs_type_child(expr->type);
            TypeSpecifier *ptr_target = cs_type_child(ltype);
            if (array_elem && ptr_target)
            {
                /* Check if element types are compatible (use cs_type_compatible for typedef aliases) */
                MeanVisitor *mean = (MeanVisitor *)visitor;
                HeaderIndex *index = mean->compiler->header_index;
                if (cs_type_compatible(array_elem, ptr_target, index) || is_void_pointer(ltype))
                {
                    CS_Creator c = creator_from_expr(expr);
                    return cs_create_array_to_pointer_expression(&c, expr, ltype);
                }
            }
        }
    }

    /* Use type compatibility for assignment (resolves typedef aliases).
     * int* and int** should NOT match, but int32_t and int should. */
    MeanVisitor *mean = (MeanVisitor *)visitor;
    HeaderIndex *index = mean->compiler->header_index;
    if (cs_type_compatible(ltype, expr->type, index))
    {
        return expr;
    }

    /* Enum type checking: different enum types cannot be mixed.
     * enum <-> int implicit conversion is allowed. */
    bool lhs_enum = cs_type_is_enum(ltype);
    bool rhs_enum = cs_type_is_enum(expr->type);
    if (lhs_enum && rhs_enum)
    {
        /* Both are enums - must be the same enum type */
        if (!cs_type_equals(ltype, expr->type))
        {
            char message[256];
            char lhs_type_str[CS_TYPE_STRING_MAX];
            char rhs_type_str[CS_TYPE_STRING_MAX];
            describe_type(ltype, lhs_type_str, sizeof lhs_type_str);
            describe_type(expr->type, rhs_type_str, sizeof rhs_type_str);
            format_expr_error(message, sizeof message, expr,
                              "cannot assign %s to %s; different enum types",
                              rhs_type_str, lhs_type_str);
            add_check_log(message, visitor);
            return expr;
        }
        return expr; /* Same enum type - OK */
    }

    /* enum <-> int implicit conversion is allowed */
    if ((lhs_enum && cs_type_is_integral(expr->type)) ||
        (cs_type_is_integral(ltype) && rhs_enum))
    {
        return expr;
    }

    /* Numeric type conversions for assignment.
     * JVM has separate instructions for int and long, so we must convert.
     * Resolve typedef names (e.g., uint32_t -> unsigned int) for numeric checks. */
    TypeSpecifier *ltype_canonical = cs_type_canonical(ltype, index);
    TypeSpecifier *rtype_canonical = cs_type_canonical(expr->type, index);
    bool lhs_numeric = cs_type_is_numeric(ltype_canonical);
    bool rhs_numeric = cs_type_is_numeric(rtype_canonical);

    if (lhs_numeric && rhs_numeric)
    {
        /* For integer literals, check if value fits in target type (like switch/case).
         * This catches cases like: unsigned int x = -1; */
        if (cs_type_is_integral(ltype_canonical))
        {
            long literal_value = 0;
            bool value_is_unsigned = false;
            bool is_literal = true;

            switch (expr->kind)
            {
            case INT_EXPRESSION:
                literal_value = expr->u.int_value;
                value_is_unsigned = false;
                break;
            case UINT_EXPRESSION:
            {
                unsigned int uval = (unsigned int)expr->u.int_value;
                literal_value = (long)uval;
                value_is_unsigned = true;
                break;
            }
            case LONG_EXPRESSION:
                literal_value = expr->u.long_value;
                value_is_unsigned = false;
                break;
            case ULONG_EXPRESSION:
                literal_value = expr->u.long_value;
                value_is_unsigned = true;
                break;
            default:
                is_literal = false;
                break;
            }

            if (is_literal)
            {
                if (cs_type_value_fits_in(literal_value, value_is_unsigned, ltype_canonical))
                {
                    /* Literal fits in target type - OK.
                     * But don't return early - we still need to check if JVM type
                     * conversion is needed (e.g., int literal 100 -> long variable
                     * needs i2l instruction). Let the widening check below handle it. */
                }
                else
                {
                    /* Literal out of range */
                    char message[256];
                    char lhs_type_str[CS_TYPE_STRING_MAX];
                    describe_type(ltype, lhs_type_str, sizeof lhs_type_str);
                    format_expr_error(message, sizeof message, expr,
                                      "value %ld is out of range for type %s",
                                      literal_value, lhs_type_str);
                    add_check_log(message, visitor);
                    return expr;
                }
            }
        }

        /* Same type - no conversion needed.
         * But must check signedness too - int and uint are different! */
        if (cs_type_same_basic(ltype_canonical, rtype_canonical) &&
            cs_type_signedness_matches(ltype_canonical, rtype_canonical))
        {
            return expr;
        }

        /* Check for narrowing within char/short/int (all stored as int on JVM) */
        bool lhs_small_int = cs_type_is_char_exact(ltype_canonical) || cs_type_is_short_exact(ltype_canonical);
        bool rhs_small_int = cs_type_is_char_exact(rtype_canonical) || cs_type_is_short_exact(rtype_canonical);
        bool lhs_is_int = cs_type_is_int_exact(ltype_canonical);
        bool rhs_is_int = cs_type_is_int_exact(rtype_canonical);

        /* char/short/int all stored as int on JVM stack.
         * For int/uint at the same size, allow implicit conversion since there's no
         * runtime difference - JVM treats both as 32-bit values.
         * Only operations with different semantics (<, >, <=, >=, /, %) check signedness. */
        if ((lhs_small_int || lhs_is_int) && (rhs_small_int || rhs_is_int))
        {
            /* For same-size int/uint, allow implicit conversion (no runtime check anyway).
             * For narrowing (e.g., short/ushort), still require matching signedness. */
            bool lhs_int_size = cs_type_is_int_exact(ltype_canonical);
            bool rhs_int_size = cs_type_is_int_exact(rtype_canonical);
            bool same_size = (lhs_int_size && rhs_int_size) ||
                             (lhs_small_int && rhs_small_int &&
                              cs_type_same_basic(ltype_canonical, rtype_canonical));

            /* Narrowing: int -> char/short, short -> char */
            bool is_narrowing = (lhs_small_int && rhs_is_int) ||
                                (cs_type_is_char_exact(ltype_canonical) && cs_type_is_short_exact(rtype_canonical));

            /* For widening (small_int -> int/uint), allow even with signedness mismatch
             * because: ushort -> uint (widening) -> int (same-size implicit) is valid,
             * and short -> int (widening) -> uint (same-size implicit) is also valid.
             * For narrowing, check literals first - if literal fits, allow regardless of signedness. */

            if (is_narrowing)
            {
                /* Check if RHS is a constant (literal or enum) that fits in target type */
                if (expr->kind == INT_EXPRESSION)
                {
                    long value = expr->u.int_value;
                    bool fits = cs_type_value_fits_in(value, false, ltype_canonical);
                    if (fits)
                    {
                        return expr;
                    }
                }
                else if (expr->kind == UINT_EXPRESSION)
                {
                    unsigned int uval = (unsigned int)expr->u.int_value;
                    long value = (long)uval;
                    bool fits = cs_type_value_fits_in(value, true, ltype_canonical);
                    if (fits)
                    {
                        return expr;
                    }
                }
                else if (expr->kind == IDENTIFIER_EXPRESSION && cs_type_is_enum(rtype_canonical))
                {
                    /* Enum constant - check if value fits in target type */
                    EnumMember *em = cs_lookup_enum_member(mean->compiler, expr->u.identifier.name);
                    if (em)
                    {
                        long value = em->value;
                        bool fits = cs_type_value_fits_in(value, false, ltype_canonical);
                        if (fits)
                        {
                            return expr;
                        }
                    }
                }
                /* Narrowing (e.g., short = int) - reject like Java unless compound assignment.
                 * Java compound assignment (e.g., c += i) is equivalent to c = (char)(c + i),
                 * which allows implicit narrowing. */
                if (!allow_narrowing)
                {
                    char message[256];
                    char lhs_type_str[CS_TYPE_STRING_MAX];
                    char rhs_type_str[CS_TYPE_STRING_MAX];
                    describe_type(ltype, lhs_type_str, sizeof lhs_type_str);
                    describe_type(expr->type, rhs_type_str, sizeof rhs_type_str);
                    format_expr_error(message, sizeof message, expr,
                                      "narrowing conversion from %s to %s requires explicit cast",
                                      rhs_type_str, lhs_type_str);
                    add_check_log(message, visitor);
                }
            }
            /* For char/short/int on JVM, no actual conversion needed - all stored as int */
            return expr;
        }

        /* Mixed signedness is allowed for assignment.
         * Two's complement representation means same-size signed/unsigned
         * have identical bit patterns, so no conversion is needed.
         * For different sizes, use widening. Narrowing requires explicit cast. */
        if (!cs_type_signedness_matches(ltype_canonical, rtype_canonical))
        {
            /* Same size: no conversion needed (e.g., int <-> uint) */
            if ((cs_type_is_int_exact(ltype_canonical) && cs_type_is_int_exact(rtype_canonical)) ||
                (cs_type_is_long_exact(ltype_canonical) && cs_type_is_long_exact(rtype_canonical)) ||
                (cs_type_is_char_exact(ltype_canonical) && cs_type_is_char_exact(rtype_canonical)) ||
                (cs_type_is_short_exact(ltype_canonical) && cs_type_is_short_exact(rtype_canonical)))
            {
                return expr;
            }

            /* Different sizes: need widening cast (e.g., int -> ulong) */
            CS_CastType ctype = cs_type_widening_cast_to(rtype_canonical, ltype_canonical);
            if (ctype != 0)
            {
                return insert_widening_to_type(expr, ltype, ctype);
            }

            /* Narrowing with signedness mismatch (e.g., ulong -> int): fall through
             * to the narrowing check below, don't silently allow */
        }

        /* Widening to target type (same signedness) */
        if (cs_type_needs_widening_to(rtype_canonical, ltype_canonical))
        {
            CS_CastType ctype = cs_type_widening_cast_to(rtype_canonical, ltype_canonical);
            if (ctype != 0)
            {
                return insert_widening_to_type(expr, ltype, ctype);
            }
        }

        /* Narrowing: larger -> smaller (allowed in C, need explicit cast in JVM) */
        bool lhs_small = lhs_small_int || lhs_is_int;
        if (true)
        {
            CS_Creator c = creator_from_expr(expr);
            /* long -> int/short/char */
            if (cs_type_is_long_exact(rtype_canonical) && lhs_small)
            {
                /* Check if RHS is a long literal that fits in target type */
                if (expr->kind == LONG_EXPRESSION)
                {
                    long value = expr->u.long_value;
                    bool fits = false;
                    if (cs_type_is_char_exact(ltype_canonical))
                    {
                        fits = (value >= 0 && value <= 255);
                    }
                    else if (cs_type_is_short_exact(ltype_canonical))
                    {
                        fits = (value >= -32768 && value <= 32767);
                    }
                    else if (cs_type_is_int_exact(ltype_canonical))
                    {
                        fits = (value >= -2147483648L && value <= 2147483647L);
                    }
                    if (fits)
                    {
                        /* Return without warning - literal fits in target type */
                        Expression *cast = cs_create_cast_expression(&c, CS_LONG_TO_INT, expr);
                        cast->type = cs_create_type_specifier(CS_INT_TYPE);
                        return cast;
                    }
                }
                /* Narrowing from long requires explicit cast */
                if (!allow_narrowing)
                {
                    char message[256];
                    char lhs_type_str[CS_TYPE_STRING_MAX];
                    describe_type(ltype, lhs_type_str, sizeof lhs_type_str);
                    format_expr_error(message, sizeof message, expr,
                                      "narrowing conversion from long to %s requires explicit cast",
                                      lhs_type_str);
                    add_check_log(message, visitor);
                }
                Expression *cast = cs_create_cast_expression(&c, CS_LONG_TO_INT, expr);
                cast->type = cs_create_type_specifier(CS_INT_TYPE);
                return cast;
            }
            /* float -> int/long */
            if (cs_type_is_float_exact(rtype_canonical))
            {
                if (cs_type_is_long_exact(ltype_canonical))
                {
                    Expression *cast = cs_create_cast_expression(&c, CS_FLOAT_TO_LONG, expr);
                    cast->type = cs_create_type_specifier(CS_LONG_TYPE);
                    return cast;
                }
                else
                {
                    Expression *cast = cs_create_cast_expression(&c, CS_FLOAT_TO_INT, expr);
                    cast->type = cs_create_type_specifier(CS_INT_TYPE);
                    return cast;
                }
            }
            /* double -> int/long/float */
            if (cs_type_is_double_exact(rtype_canonical))
            {
                if (cs_type_is_long_exact(ltype_canonical))
                {
                    Expression *cast = cs_create_cast_expression(&c, CS_DOUBLE_TO_LONG, expr);
                    cast->type = cs_create_type_specifier(CS_LONG_TYPE);
                    return cast;
                }
                else if (cs_type_is_float_exact(ltype_canonical))
                {
                    Expression *cast = cs_create_cast_expression(&c, CS_DOUBLE_TO_FLOAT, expr);
                    cast->type = cs_create_type_specifier(CS_FLOAT_TYPE);
                    return cast;
                }
                else
                {
                    Expression *cast = cs_create_cast_expression(&c, CS_DOUBLE_TO_INT, expr);
                    cast->type = cs_create_type_specifier(CS_INT_TYPE);
                    return cast;
                }
            }
        }

        return expr;
    }
    else
    {
        char message[256];
        char lhs_type[CS_TYPE_STRING_MAX];
        char rhs_type[CS_TYPE_STRING_MAX];
        describe_type(ltype, lhs_type, sizeof lhs_type);
        describe_type(expr->type, rhs_type, sizeof rhs_type);
        format_expr_error(message, sizeof message, expr,
                          "assignment type error %s = %s",
                          lhs_type,
                          rhs_type);
        add_check_log(message, visitor);
    }
    return expr;
}
static void enter_assignexpr(Expression *expr, Visitor *visitor) {}
static void leave_assignexpr(Expression *expr, Visitor *visitor)
{
    DBG_PRINT("DEBUG: leave_assignexpr\n");
    fflush(stderr);
    Expression *left = expr->u.assignment_expression.left;
    Expression *right = expr->u.assignment_expression.right;
    AssignmentOperator aope = expr->u.assignment_expression.aope;

    /* Handle pointer compound assignment: ptr += int, ptr -= int */
    if ((aope == ADD_ASSIGN || aope == SUB_ASSIGN) &&
        cs_type_is_pointer(left->type))
    {
        /* For ptr += int or ptr -= int, right must be integral type */
        if (!cs_type_is_integral(right->type))
        {
            char message[256];
            char rhs_type[CS_TYPE_STRING_MAX];
            describe_type(right->type, rhs_type, sizeof rhs_type);
            format_expr_error(message, sizeof message, expr,
                              "pointer arithmetic requires integer operand, got %s",
                              rhs_type);
            add_check_log(message, visitor);
        }
        /* Result type is the pointer type */
        expr->type = left->type;
        return;
    }

    /* Compound assignments (+=, -=, etc.) allow implicit narrowing like Java.
     * Java: c += i is equivalent to c = (char)(c + i), allowing narrowing. */
    bool is_compound = (aope != ASSIGN);
    expr->u.assignment_expression.right =
        assignment_type_check(left->type, right, visitor, is_compound);
    expr->type = left->type;
}

/* Mark function call target identifier before children are visited.
 * This ensures the identifier is properly tagged as a function before
 * leave_identifierexpr runs, preventing it from being treated as a variable. */
static void enter_funccallexpr(Expression *expr, Visitor *visitor)
{
    MeanVisitor *mean = (MeanVisitor *)visitor;
    Expression *func_expr = expr->u.function_call_expression.function;

    if (func_expr && func_expr->kind == IDENTIFIER_EXPRESSION)
    {
        const char *name = func_expr->u.identifier.name;
        if (name && !func_expr->u.identifier.is_function)
        {
            FunctionDeclaration *func = cs_search_function(mean->compiler, name);
            if (func)
            {
                func_expr->u.identifier.u.function = func;
                func_expr->u.identifier.is_function = true;
            }
        }
    }
}
static void leave_funccallexpr(Expression *expr, Visitor *visitor)
{
    MeanVisitor *mean = (MeanVisitor *)visitor;
    Expression *func_expr = expr->u.function_call_expression.function;
    ArgumentList *call_args = expr->u.function_call_expression.argument;
    FunctionDeclaration *func_dec = NULL;
    DBG_PRINT("type = %d\n", func_expr->kind);

    /* Handle va_start/va_arg/va_end as built-in functions */
    if (func_expr->kind == IDENTIFIER_EXPRESSION)
    {
        const char *name = func_expr->u.identifier.name;
        if (name && strcmp(name, "va_start") == 0)
        {
            /* va_start(ap): ap must be va_list (void**), returns void */
            ArgumentList *args = call_args;
            if (!args || args->next)
            {
                char message[256];
                format_expr_error(message, sizeof message, expr,
                                  "va_start requires exactly 1 argument");
                add_check_log(message, visitor);
            }
            else if (args->expr && args->expr->type &&
                     !cs_type_is_pointer(args->expr->type))
            {
                char message[256];
                format_expr_error(message, sizeof message, expr,
                                  "va_start argument must be va_list type");
                add_check_log(message, visitor);
            }
            expr->type = cs_create_type_specifier(CS_VOID_TYPE);
            return;
        }
        else if (name && strcmp(name, "__builtin_va_arg") == 0)
        {
            /* __builtin_va_arg(ap, sizeof(T)): ap must be va_list (void**), returns T */
            ArgumentList *args = call_args;
            if (!args || !args->next || args->next->next)
            {
                char message[256];
                format_expr_error(message, sizeof message, expr,
                                  "va_arg requires exactly 2 arguments");
                add_check_log(message, visitor);
                expr->type = cs_create_type_specifier(CS_INT_TYPE);
                return;
            }
            if (args->expr && args->expr->type &&
                !cs_type_is_pointer(args->expr->type))
            {
                char message[256];
                format_expr_error(message, sizeof message, expr,
                                  "va_arg first argument must be va_list type");
                add_check_log(message, visitor);
            }
            /* Second argument should be sizeof(T), extract T */
            Expression *sizeof_expr = args->next->expr;
            if (sizeof_expr && sizeof_expr->kind == SIZEOF_EXPRESSION &&
                sizeof_expr->u.sizeof_expression.is_type)
            {
                expr->type = sizeof_expr->u.sizeof_expression.type;
            }
            else
            {
                char message[256];
                format_expr_error(message, sizeof message, expr,
                                  "va_arg second argument must be sizeof(type)");
                add_check_log(message, visitor);
                expr->type = cs_create_type_specifier(CS_INT_TYPE);
            }
            return;
        }
        else if (name && strcmp(name, "va_end") == 0)
        {
            /* va_end(ap): ap must be va_list (void**), returns void */
            ArgumentList *args = call_args;
            if (!args || args->next)
            {
                char message[256];
                format_expr_error(message, sizeof message, expr,
                                  "va_end requires exactly 1 argument");
                add_check_log(message, visitor);
            }
            else if (args->expr && args->expr->type &&
                     !cs_type_is_pointer(args->expr->type))
            {
                char message[256];
                format_expr_error(message, sizeof message, expr,
                                  "va_end argument must be va_list type");
                add_check_log(message, visitor);
            }
            expr->type = cs_create_type_specifier(CS_VOID_TYPE);
            return;
        }
        else if (name && strcmp(name, "calloc") == 0)
        {
            /* calloc(n, sizeof(T)): second argument must be sizeof(type) */
            ArgumentList *args = call_args;
            if (args && args->next)
            {
                Expression *sizeof_expr = args->next->expr;
                if (!sizeof_expr || sizeof_expr->kind != SIZEOF_EXPRESSION ||
                    !sizeof_expr->u.sizeof_expression.is_type)
                {
                    char message[256];
                    format_expr_error(message, sizeof message, expr,
                                      "calloc second argument must be sizeof(type)");
                    add_check_log(message, visitor);
                }
            }
            /* Fall through to normal function call processing */
        }
    }

    switch (func_expr->kind)
    {
    case IDENTIFIER_EXPRESSION:
    {
        //            printf("identifier!!!\n");
        //            printf("%s\n", func_expr->u.identifier.name);
        if (func_expr->u.identifier.name == NULL)
        {
            fprintf(stderr, "Error: function name is NULL at line %d\n", expr->line_number);
            exit(1);
        }
        DBG_PRINT("DEBUG: searching function %s\n", func_expr->u.identifier.name);
        func_dec = cs_search_function(mean->compiler, func_expr->u.identifier.name);
        DBG_PRINT("DEBUG: search done\n");
        break;
    }
    default:
    {
        DBG_PRINT("this type cannot be the function %d\n", func_expr->kind);
        exit(1);
    }
    }

    if (func_dec)
    {
        /* Ensure the identifier is marked as a function */
        if (func_expr->kind == IDENTIFIER_EXPRESSION)
        {
            func_expr->u.identifier.u.function = func_dec;
            func_expr->u.identifier.is_function = true;
        }

        ParameterList *params = func_dec->param;
        ArgumentList *args = call_args;
        int args_count;
        int fixed_param_count = cs_count_parameters(func_dec->param);
        bool accepts_varargs = func_dec->is_variadic;

        for (ParameterList *scan = func_dec->param; scan; scan = scan->next)
        {
            if (scan->is_ellipsis)
            {
                accepts_varargs = true;
                break;
            }
        }

        for (args_count = 0; args; args = args->next, ++args_count)
            ;

        bool argcount_error = false;
        if (!accepts_varargs && fixed_param_count != args_count)
        {
            argcount_error = true;
        }
        else if (accepts_varargs && args_count < fixed_param_count)
        {
            argcount_error = true;
        }

        if (argcount_error)
        {
            DBG_PRINT("argument count is not the same\n");
            char message[256];
            if (accepts_varargs)
            {
                format_expr_error(
                    message, sizeof message, expr,
                    "argument count mismatch in function call require at least:%d, pass:%d",
                    fixed_param_count, args_count);
            }
            else
            {
                format_expr_error(
                    message, sizeof message, expr,
                    "argument count mismatch in function call require:%d, pass:%d",
                    fixed_param_count, args_count);
            }
            add_check_log(message, visitor);
        }
        else
        {
            for (params = func_dec->param, args = call_args; params && args;
                 params = params->next, args = args->next)
            {
                if (params->is_ellipsis)
                {
                    break;
                }
                if (!params->type || !args->expr)
                {
                    continue;
                }
                args->expr = assignment_type_check(params->type, args->expr, visitor, false);
            }
        }

        assign_expression_type(func_expr, func_dec->type, mean->compiler);
        assign_expression_type(expr, func_dec->type, mean->compiler);
    }
    else
    {
        expr->type = func_expr ? func_expr->type : NULL;
    }
}

/* For statement */
static void enter_exprstmt(Statement *stmt, Visitor *visitor)
{
    DBG_PRINT("DEBUG: enter_exprstmt\n");
    //    fprintf(stderr, "enter exprstmt :\n");
}
static void leave_exprstmt(Statement *stmt, Visitor *visitor)
{
    DBG_PRINT("DEBUG: leave_exprstmt\n");
    fflush(stderr);
}

static void enter_compoundstmt(Statement *stmt, Visitor *visitor)
{
    DBG_PRINT("DEBUG: enter_compoundstmt\n");
    push_scope((MeanVisitor *)visitor);
}

static void leave_compoundstmt(Statement *stmt, Visitor *visitor)
{
    DBG_PRINT("DEBUG: leave_compoundstmt\n");
    pop_scope((MeanVisitor *)visitor);
}

/* Propagate type information to nested initializer list expressions.
 * This ensures each nested {...} has its type set for codegen. */
static void propagate_init_list_types(Expression *init, TypeSpecifier *type)
{
    if (!init || !type || init->kind != INITIALIZER_LIST_EXPRESSION)
        return;

    /* Set type on this initializer list */
    if (!init->type)
        init->type = type;

    /* For arrays, propagate element type to each child */
    if (cs_type_is_array(type))
    {
        TypeSpecifier *elem_type = cs_type_child(type);
        for (ExpressionList *p = init->u.initializer_list; p; p = p->next)
        {
            if (p->expression)
                propagate_init_list_types(p->expression, elem_type);
        }
    }
    /* For structs, propagate member types to each child */
    else if (cs_type_is_named(type) && cs_type_is_basic_struct_or_union(type))
    {
        StructMember *member = cs_type_struct_members(type);
        for (ExpressionList *p = init->u.initializer_list; p; p = p->next)
        {
            if (!p->expression)
                continue;

            /* Handle designated initializers */
            if (p->expression->kind == DESIGNATED_INITIALIZER_EXPRESSION)
            {
                const char *field_name = p->expression->u.designated_initializer.field_name;
                /* Find matching member */
                for (StructMember *m = cs_type_struct_members(type); m; m = m->next)
                {
                    if (m->name && field_name && strcmp(m->name, field_name) == 0)
                    {
                        propagate_init_list_types(
                            p->expression->u.designated_initializer.value, m->type);
                        break;
                    }
                }
            }
            else if (member)
            {
                propagate_init_list_types(p->expression, member->type);
                member = member->next;
            }
        }
    }
}

static void enter_declstmt(Statement *stmt, Visitor *visitor)
{
    DBG_PRINT("DEBUG: enter_declstmt\n");
    MeanVisitor *mean = (MeanVisitor *)visitor;
    if (mean->current_scope == NULL)
    {
        CS_Compiler *compiler = mean->compiler;
        Declaration *decl = stmt->u.declaration_s;
        /* Set class_name from current file if not already set */
        if (!decl->class_name && compiler->current_file_decl &&
            compiler->current_file_decl->class_name)
        {
            decl->class_name = compiler->current_file_decl->class_name;
        }
        compiler->decl_list = cs_chain_declaration(compiler->decl_list, decl);
    }
}

static void leave_declstmt(Statement *stmt, Visitor *visitor)
{
    DBG_PRINT("DEBUG: leave_declstmt\n");
    MeanVisitor *mean = (MeanVisitor *)visitor;
    Declaration *decl = stmt->u.declaration_s;
    finalize_declaration_type(decl, mean->compiler);

    /* Traverse VLA size expressions now that type is finalized */
    if (decl->type && cs_type_is_array(decl->type))
    {
        for (TypeSpecifier *t = decl->type; t && cs_type_is_array(t); t = cs_type_child(t))
        {
            Expression *size_expr = cs_type_array_size(t);
            if (size_expr && size_expr->kind != INT_EXPRESSION &&
                size_expr->kind != BOOL_EXPRESSION)
            {
                mean_traverse_expr(size_expr, mean);
                /* Convert enum constant to INT_EXPRESSION for sizeof support */
                if (size_expr->kind == IDENTIFIER_EXPRESSION)
                {
                    EnumMember *em = cs_lookup_enum_member(mean->compiler, size_expr->u.identifier.name);
                    if (em)
                    {
                        CS_Creator c = creator_from_expr(size_expr);
                        Expression *int_expr = cs_create_int_expression(&c, em->value);
                        cs_type_set_array_size(t, int_expr);
                    }
                }
            }
        }
    }

    /* Infer array size from initializer list if not explicitly specified */
    if (decl->type && cs_type_is_array(decl->type) &&
        cs_type_array_size(decl->type) == NULL &&
        decl->initializer != NULL &&
        decl->initializer->kind == INITIALIZER_LIST_EXPRESSION)
    {
        /* Count elements in initializer list */
        int count = 0;
        for (ExpressionList *p = decl->initializer->u.initializer_list; p; p = p->next)
        {
            count++;
        }
        /* Create INT_EXPRESSION for the size and set it on the type */
        CS_Creator c = creator_from_expr(decl->initializer);
        Expression *size_expr = cs_create_int_expression(&c, count);
        cs_type_set_array_size(decl->type, size_expr);

        /* Also set on parsed_type so re-resolution preserves the size */
        if (decl->parsed_type && decl->parsed_type->kind == CS_TYPE_ARRAY &&
            decl->parsed_type->array_size == NULL)
        {
            decl->parsed_type->array_size = size_expr;
        }

        /* Also update any existing declaration with the same name (e.g., extern declaration from header).
         * This ensures lookups find the correct array size. */
        Declaration *existing = cs_search_decl_global(mean->compiler, decl->name);
        if (existing && existing != decl)
        {
            if (existing->type && cs_type_is_array(existing->type) &&
                cs_type_array_size(existing->type) == NULL)
            {
                cs_type_set_array_size(existing->type, size_expr);
            }
            if (existing->parsed_type && existing->parsed_type->kind == CS_TYPE_ARRAY &&
                existing->parsed_type->array_size == NULL)
            {
                existing->parsed_type->array_size = size_expr;
            }
        }
    }

    if (decl->initializer != NULL)
    {
        decl->initializer =
            assignment_type_check(decl->type, decl->initializer, visitor, false);

        /* Propagate type info to nested initializer lists */
        propagate_init_list_types(decl->initializer, decl->type);
    }

    /* For global scope definitions (non-extern),
     * clear is_extern on any existing extern declaration with the same name
     * and update its class_name to the defining class.
     * This ensures the field gets generated in register_static_fields(). */
    if (!mean->current_scope && !decl->is_extern)
    {
        Declaration *existing = cs_search_decl_global(mean->compiler, decl->name);
        if (existing && existing != decl && existing->is_extern)
        {
            /* Found an extern declaration - clear its flag and update class_name */
            existing->is_extern = false;
            if (decl->class_name)
            {
                existing->class_name = decl->class_name;
            }
        }
    }

    add_decl_to_scope((MeanVisitor *)visitor, decl);
}

static void enter_generic_stmt(Statement *stmt, Visitor *visitor)
{
    (void)stmt;
    (void)visitor;
}

static void enter_generic_expr(Expression *expr, Visitor *visitor)
{
    (void)expr;
    (void)visitor;
}

static void leave_generic_expr(Expression *expr, Visitor *visitor)
{
    (void)expr;
    (void)visitor;
}

static void enter_sizeofexpr(Expression *expr, Visitor *visitor)
{
    (void)expr;
    (void)visitor;
}

static void leave_sizeofexpr(Expression *expr, Visitor *visitor)
{
    MeanVisitor *mean = (MeanVisitor *)visitor;
    char message[256];
    expr->type = cs_create_type_specifier(CS_INT_TYPE);
    expr->u.sizeof_expression.computed_value = 0;

    /* For sizeof(type), computed_value stays 0 (used only for calloc) */
    if (expr->u.sizeof_expression.is_type)
    {
        if (expr->parsed_type)
        {
            expr->u.sizeof_expression.type =
                resolve_parsed_type(mean->compiler, expr->parsed_type);
        }
        return;
    }

    /* For sizeof identifier, sizeof *arr, etc. */
    Expression *inner = expr->u.sizeof_expression.expr;
    if (!inner)
    {
        return;
    }

    if (inner->kind == IDENTIFIER_EXPRESSION)
    {
        /* sizeof arr - return total array size (product of all dimensions) */
        TypeSpecifier *id_type = inner->type;
        int size = cs_type_compute_array_size(id_type);
        if (size > 0)
        {
            expr->u.sizeof_expression.computed_value = size;
        }
        else
        {
            format_expr_error(message, sizeof message, inner,
                              "sizeof identifier requires array type with constant dimensions");
            add_check_log(message, (Visitor *)mean);
        }
    }
    else if (inner->kind == DEREFERENCE_EXPRESSION)
    {
        /* sizeof *expr - check what we're dereferencing */
        Expression *operand = inner->u.dereference_expression;
        TypeSpecifier *operand_type = operand ? operand->type : NULL;

        if (!operand_type)
        {
            format_expr_error(message, sizeof message, inner,
                              "sizeof dereference: cannot determine operand type");
            add_check_log(message, (Visitor *)mean);
            return;
        }

        if (cs_type_is_pointer(operand_type))
        {
            /* sizeof *ptr - pointer dereference result is not allowed */
            format_expr_error(message, sizeof message, inner,
                              "sizeof of pointer dereference is not allowed");
            add_check_log(message, (Visitor *)mean);
            return;
        }

        if (cs_type_is_array(operand_type))
        {
            /* sizeof *arr - compute size of dereferenced type */
            TypeSpecifier *deref_type = inner->type;
            int size = cs_type_compute_array_size(deref_type);
            if (size > 0)
            {
                expr->u.sizeof_expression.computed_value = size;
            }
            else
            {
                /* Dereferenced to non-array type (basic, struct, union).
                 * In Cminor's sizeof semantics for idiom sizeof arr / sizeof *arr,
                 * the element size is 1. */
                expr->u.sizeof_expression.computed_value = 1;
            }
            return;
        }

        format_expr_error(message, sizeof message, inner,
                          "sizeof dereference requires array type");
        add_check_log(message, (Visitor *)mean);
    }
    else
    {
        format_expr_error(message, sizeof message, inner,
                          "sizeof expression not supported");
        add_check_log(message, (Visitor *)mean);
    }
}

static void check_condition_type(Expression *condition, Visitor *visitor)
{
    char message[256];

    if (condition == NULL || condition->type == NULL)
    {
        return;
    }

    /* long and double are not allowed as condition types (JVM limitation) */
    if (cs_type_is_long_exact(condition->type))
    {
        format_expr_error(message, sizeof message, condition,
                          "long type not allowed in condition; use explicit comparison (e.g., x != 0)");
        add_check_log(message, visitor);
        return;
    }
    if (cs_type_is_double_exact(condition->type))
    {
        format_expr_error(message, sizeof message, condition,
                          "double type not allowed in condition; use explicit comparison (e.g., x != 0.0)");
        add_check_log(message, visitor);
        return;
    }
    if (cs_type_is_float_exact(condition->type))
    {
        format_expr_error(message, sizeof message, condition,
                          "float type not allowed in condition; use explicit comparison (e.g., x != 0.0f)");
        add_check_log(message, visitor);
        return;
    }
}

static void leave_generic_stmt(Statement *stmt, Visitor *visitor)
{
    Expression *condition = NULL;

    switch (stmt->type)
    {
    case IF_STATEMENT:
        condition = stmt->u.if_s.condition;
        break;
    case WHILE_STATEMENT:
        condition = stmt->u.while_s.condition;
        break;
    case DO_WHILE_STATEMENT:
        condition = stmt->u.do_s.condition;
        break;
    case FOR_STATEMENT:
        condition = stmt->u.for_s.condition;
        break;
    default:
        return;
    }

    check_condition_type(condition, visitor);
}

/* Push switch expression type onto stack */
static void push_switch_type(MeanVisitor *visitor, TypeSpecifier *type)
{
    SwitchTypeStack *node = (SwitchTypeStack *)calloc(1, sizeof(SwitchTypeStack));
    node->expr_type = type;
    node->next = visitor->switch_type_stack;
    visitor->switch_type_stack = node;
}

/* Pop switch expression type from stack */
static void pop_switch_type(MeanVisitor *visitor)
{
    if (visitor->switch_type_stack)
    {
        SwitchTypeStack *old = visitor->switch_type_stack;
        visitor->switch_type_stack = old->next;
        free(old);
    }
}

/* Get current switch expression type */
static TypeSpecifier *current_switch_type(MeanVisitor *visitor)
{
    return visitor->switch_type_stack ? visitor->switch_type_stack->expr_type : NULL;
}

static void enter_switchstmt(Statement *stmt, Visitor *visitor)
{
    (void)stmt;
    (void)visitor;
    /* Type is pushed after expression is traversed */
}

static void leave_switchstmt(Statement *stmt, Visitor *visitor)
{
    (void)stmt;
    MeanVisitor *mean = (MeanVisitor *)visitor;
    pop_switch_type(mean);
}

static void leave_casestmt(Statement *stmt, Visitor *visitor)
{
    MeanVisitor *mean = (MeanVisitor *)visitor;
    Expression *case_expr = stmt->u.case_s.expression;
    TypeSpecifier *switch_type = current_switch_type(mean);

    if (!case_expr || !switch_type)
    {
        return;
    }

    TypeSpecifier *case_type = case_expr->type;
    if (!case_type)
    {
        return;
    }

    /* Check type compatibility between switch expression and case.
     * In C, enum and int are compatible in switch/case contexts. */
    if (!cs_type_switch_compatible(switch_type, case_type))
    {
        char message[256];
        char switch_type_str[CS_TYPE_STRING_MAX];
        char case_type_str[CS_TYPE_STRING_MAX];
        describe_type(switch_type, switch_type_str, sizeof switch_type_str);
        describe_type(case_type, case_type_str, sizeof case_type_str);
        format_expr_error(message, sizeof message, case_expr,
                          "case type %s is not compatible with switch type %s",
                          case_type_str, switch_type_str);
        add_check_log(message, visitor);
        return;
    }

    /* For integral types, check if case value fits in switch type's range */
    if (cs_type_is_integral(switch_type) && cs_type_is_integral(case_type))
    {
        long case_value = 0;
        bool value_is_unsigned = false;

        /* Extract constant value from case expression */
        switch (case_expr->kind)
        {
        case INT_EXPRESSION:
            case_value = case_expr->u.int_value;
            value_is_unsigned = false;
            break;
        case UINT_EXPRESSION:
        {
            unsigned int uval = (unsigned int)case_expr->u.int_value;
            case_value = (long)uval;
            value_is_unsigned = true;
            break;
        }
        case LONG_EXPRESSION:
            case_value = case_expr->u.long_value;
            value_is_unsigned = false;
            break;
        case ULONG_EXPRESSION:
            case_value = case_expr->u.long_value;
            value_is_unsigned = true;
            break;
        default:
            /* Non-constant case expression - handled elsewhere */
            return;
        }

        if (!cs_type_value_fits_in(case_value, value_is_unsigned, switch_type))
        {
            char message[256];
            char switch_type_str[CS_TYPE_STRING_MAX];
            describe_type(switch_type, switch_type_str, sizeof switch_type_str);
            format_expr_error(message, sizeof message, case_expr,
                              "case value %ld is out of range for switch type %s",
                              case_value, switch_type_str);
            add_check_log(message, visitor);
        }
    }
}

static void enter_arrayexpr(Expression *expr, Visitor *visitor) {}
static void leave_arrayexpr(Expression *expr, Visitor *visitor)
{
    Expression *array = expr->u.array_expression.array;
    Expression *index = expr->u.array_expression.index;
    char message[256];

    if (array->type == NULL)
    {
        format_expr_error(message, sizeof message, expr,
                          "Cannot find array type");
        add_check_log(message, visitor);
        return;
    }

    if (index->type == NULL)
    {
        format_expr_error(message, sizeof message, expr,
                          "Cannot find index type");
        add_check_log(message, visitor);
        return;
    }

    if (!cs_type_is_array(array->type) && !cs_type_is_pointer(array->type))
    {
        format_expr_error(message, sizeof message, expr,
                          "Operand is not array or pointer");
        add_check_log(message, visitor);
        return;
    }

    /* Array index must be convertible to int (same rules as assignment).
     * This allows enum types as well (enum -> int implicit conversion).
     * Use assignment_type_check with int target to unify the rules. */
    TypeSpecifier *int_type = cs_create_type_specifier(CS_INT_TYPE);
    expr->u.array_expression.index = assignment_type_check(int_type, index, visitor, false);

    DBG_PRINT("DEBUG: line %d array->type = %p\n", expr->line_number, array->type);
    if (array->type)
    {
        DBG_PRINT("DEBUG: array->type->kind = %d\n", cs_type_kind(array->type));
        DBG_PRINT("DEBUG: array->type->child = %p\n", cs_type_child(array->type));
    }
    fflush(stderr);

    if (array->type && cs_type_child(array->type))
    {
        expr->type = cs_copy_type_specifier(cs_type_child(array->type));
    }
    else
    {
        expr->type = NULL;
    }
}

static void enter_conditionalexpr(Expression *expr, Visitor *visitor) {}
static void leave_conditionalexpr(Expression *expr, Visitor *visitor)
{
    Expression *condition = expr->u.conditional_expression.condition;
    Expression *true_expr = expr->u.conditional_expression.true_expr;
    Expression *false_expr = expr->u.conditional_expression.false_expr;
    char message[256];

    if (condition->type == NULL)
    {
        format_expr_error(message, sizeof message, expr,
                          "Cannot find condition type");
        add_check_log(message, visitor);
        return;
    }
    if (true_expr->type == NULL)
    {
        format_expr_error(message, sizeof message, expr,
                          "Cannot find true expression type");
        add_check_log(message, visitor);
        return;
    }
    if (false_expr->type == NULL)
    {
        format_expr_error(message, sizeof message, expr,
                          "Cannot find false expression type");
        add_check_log(message, visitor);
        return;
    }

    /* long, double, float are not allowed as condition types (JVM limitation) */
    if (cs_type_is_long_exact(condition->type))
    {
        format_expr_error(message, sizeof message, expr,
                          "long type not allowed in ternary condition; use explicit comparison (e.g., x != 0)");
        add_check_log(message, visitor);
        return;
    }
    if (cs_type_is_double_exact(condition->type))
    {
        format_expr_error(message, sizeof message, expr,
                          "double type not allowed in ternary condition; use explicit comparison (e.g., x != 0.0)");
        add_check_log(message, visitor);
        return;
    }
    if (cs_type_is_float_exact(condition->type))
    {
        format_expr_error(message, sizeof message, expr,
                          "float type not allowed in ternary condition; use explicit comparison (e.g., x != 0.0f)");
        add_check_log(message, visitor);
        return;
    }

    /* Condition must be bool, integral (int/char/short), or reference (pointer/array).
     * enum, float, double, long, and struct values are not allowed. */
    if (!cs_type_is_bool(condition->type) &&
        !cs_type_is_integral(condition->type) &&
        !cs_type_is_pointer(condition->type) &&
        !cs_type_is_array(condition->type))
    {
        char cond_type_str[CS_TYPE_STRING_MAX];
        describe_type(condition->type, cond_type_str, sizeof cond_type_str);
        format_expr_error(message, sizeof message, expr,
                          "condition must be bool, integer, or pointer but found %s",
                          cond_type_str);
        add_check_log(message, visitor);
        return;
    }

    /* Both branches must have compatible types */
    if (!cs_type_equals(true_expr->type, false_expr->type))
    {
        /* Allow void* (NULL) with any pointer type */
        bool true_is_void_ptr = is_void_pointer(true_expr->type);
        bool false_is_void_ptr = is_void_pointer(false_expr->type);
        bool true_is_pointer = cs_type_is_pointer(true_expr->type);
        bool false_is_pointer = cs_type_is_pointer(false_expr->type);

        if (true_is_void_ptr && false_is_pointer)
        {
            /* NULL : some_ptr* → result is some_ptr* */
            expr->type = cs_copy_type_specifier(false_expr->type);
            /* Propagate type to NULL expression for proper JVM bytecode generation */
            true_expr->type = cs_copy_type_specifier(false_expr->type);
            return;
        }
        else if (false_is_void_ptr && true_is_pointer)
        {
            /* some_ptr* : NULL → result is some_ptr* */
            expr->type = cs_copy_type_specifier(true_expr->type);
            /* Propagate type to NULL expression for proper JVM bytecode generation */
            false_expr->type = cs_copy_type_specifier(true_expr->type);
            return;
        }

        /* Allow compatible numeric types via usual arithmetic conversions */
        if (cs_type_is_numeric(true_expr->type) && cs_type_is_numeric(false_expr->type))
        {
            if (!cs_type_can_mix_for_comparison(true_expr->type, false_expr->type))
            {
                char true_type_str[CS_TYPE_STRING_MAX];
                char false_type_str[CS_TYPE_STRING_MAX];
                describe_type(true_expr->type, true_type_str, sizeof true_type_str);
                describe_type(false_expr->type, false_type_str, sizeof false_type_str);
                format_expr_error(
                    message, sizeof message, expr,
                    "signed/unsigned mismatch in conditional: %s and %s; use explicit cast",
                    true_type_str, false_type_str);
                add_check_log(message, visitor);
                return;
            }

            /* Determine common type using binary numeric promotion */
            TypeSpecifier *common_type = cs_type_binary_promoted_specifier(
                true_expr->type, false_expr->type);
            if (!common_type)
            {
                format_expr_error(message, sizeof message, expr,
                                  "Cannot determine common type in conditional expression");
                add_check_log(message, visitor);
                return;
            }

            /* Insert widening casts if needed */
            CS_CastType true_cast = cs_type_widening_cast_to(true_expr->type, common_type);
            CS_CastType false_cast = cs_type_widening_cast_to(false_expr->type, common_type);

            if (true_cast != 0)
            {
                expr->u.conditional_expression.true_expr =
                    insert_widening_to_type(true_expr, common_type, true_cast);
            }
            if (false_cast != 0)
            {
                expr->u.conditional_expression.false_expr =
                    insert_widening_to_type(false_expr, common_type, false_cast);
            }

            expr->type = common_type;
            return;
        }

        format_expr_error(message, sizeof message, expr,
                          "Type mismatch in conditional expression");
        add_check_log(message, visitor);
        return;
    }

    expr->type = cs_copy_type_specifier(true_expr->type);
}

static void enter_addrexpr(Expression *expr, Visitor *visitor) {}
static void leave_addrexpr(Expression *expr, Visitor *visitor)
{
    Expression *target = expr->u.address_expression;
    char message[256];

    if (target->type == NULL)
    {
        format_expr_error(message, sizeof message, expr,
                          "Cannot find address target type");
        add_check_log(message, visitor);
        return;
    }

    bool requires_heap_lift = false;
    TypeSpecifier *ref_type =
        cs_type_reference_for_address(target->type, &requires_heap_lift);

    if (!ref_type)
    {
        format_expr_error(message, sizeof message, expr,
                          "Unsupported address target type");
        add_check_log(message, visitor);
        return;
    }

    if (requires_heap_lift && target->kind == IDENTIFIER_EXPRESSION &&
        !target->u.identifier.is_function)
    {
        Declaration *decl = target->u.identifier.u.declaration;
        /* Only heap-lift local variables, not global/static variables.
         * Global variables have class_name set (they become static fields).
         * Static local variables also cannot be heap-lifted. */
        if (decl && !decl->class_name && !decl->is_static)
        {
            decl->needs_heap_lift = true;
            DBG_PRINT("DEBUG: marking variable '%s' for heap lift\n", decl->name);
        }
    }

    /* Check for unsupported heap-lift cases and emit explicit errors.
     * Note: ARRAY_EXPRESSION (&a[i]) is safe because arrays are already references,
     * and &a[i] is just pointer arithmetic.
     * Note: MEMBER_EXPRESSION (&p->member) where p is a pointer is also safe,
     * because the struct is already on the heap. Only warn for stack-based members. */
    if (requires_heap_lift && target->kind != IDENTIFIER_EXPRESSION &&
        target->kind != ARRAY_EXPRESSION)
    {
        /* For MEMBER_EXPRESSION, check if the base is a pointer (heap-based) */
        if (target->kind == MEMBER_EXPRESSION)
        {
            /* If via_pointer is true (e.g., p->member), it's heap-based and safe */
            if (target->u.member_expression.via_pointer)
            {
                /* Safe: heap-based member access */
                expr->type = ref_type;
                return;
            }

            Expression *base = target->u.member_expression.target;
            /* If the base is itself a pointer dereference or member access through pointer,
             * it's heap-based and safe. Only warn for direct identifier access like s.member. */
            if (base && (base->kind == DEREFERENCE_EXPRESSION ||
                         base->kind == MEMBER_EXPRESSION ||
                         base->kind == ARRAY_EXPRESSION))
            {
                /* Safe: heap-based member access */
                expr->type = ref_type;
                return;
            }
        }

        const char *kind_name = "unknown";
        switch (target->kind)
        {
        case MEMBER_EXPRESSION:
            kind_name = "stack-based struct member (&s.member)";
            break;
        case DEREFERENCE_EXPRESSION:
            kind_name = "dereferenced pointer (&*p)";
            break;
        default:
            kind_name = "complex expression";
            break;
        }
        format_expr_error(message, sizeof message, expr,
                          "Address-of %s requires heap-lift (not supported)", kind_name);
        add_check_log(message, visitor);
        fprintf(stderr, "%s\n", message);
        fprintf(stderr, "       This will cause pointer writes to fail in self-compiled code.\n");
        fprintf(stderr, "       Workaround: Use a temporary variable.\n");
    }

    expr->type = ref_type;
}

static void enter_derefexpr(Expression *expr, Visitor *visitor) {}
static void leave_derefexpr(Expression *expr, Visitor *visitor)
{
    Expression *target = expr->u.dereference_expression;
    char message[256];

    if (target->type == NULL)
    {
        format_expr_error(message, sizeof message, expr,
                          "Cannot find dereference target type");
        add_check_log(message, visitor);
        return;
    }

    /* Check if target is a pointer or array (dereferenceable types) */
    if (!cs_type_is_pointer(target->type) && !cs_type_is_array(target->type))
    {
        format_expr_error(message, sizeof message, expr,
                          "Operand is not a reference type");
        add_check_log(message, visitor);
        return;
    }

    if (cs_type_child(target->type))
    {
        /* Dereferencing should drop exactly one pointer/array level */
        expr->type = cs_copy_type_specifier(cs_type_child(target->type));
    }
    else
    {
        format_expr_error(message, sizeof message, expr,
                          "Reference type has no target element");
        add_check_log(message, visitor);
    }
}

static void enter_memberexpr(Expression *expr, Visitor *visitor) {}
static void leave_memberexpr(Expression *expr, Visitor *visitor)
{
    MeanVisitor *mean = (MeanVisitor *)visitor;
    Expression *target = expr->u.member_expression.target;
    char *member_name = expr->u.member_expression.member_name;
    bool via_pointer = expr->u.member_expression.via_pointer;
    char message[256];

    if (target->type == NULL)
    {
        format_expr_error(message, sizeof message, expr,
                          "Cannot find member target type");
        add_check_log(message, visitor);
        return;
    }
    TypeSpecifier *struct_type = target->type;
    if (via_pointer)
    {
        if (!cs_type_is_pointer(struct_type))
        {
            format_expr_error(message, sizeof message, expr,
                              "Pointer member access requires pointer type");
            add_check_log(message, visitor);
            return;
        }
        struct_type = cs_type_child(struct_type);
    }
    else if (cs_type_is_pointer(struct_type))
    {
        format_expr_error(message, sizeof message, expr,
                          "Use -> to access pointer members");
        add_check_log(message, visitor);
        return;
    }

    if (struct_type == NULL)
    {
        format_expr_error(message, sizeof message, expr,
                          "Cannot resolve struct type for member access");
        add_check_log(message, visitor);
        return;
    }

    StructMember *member = cs_lookup_struct_member(mean->compiler, struct_type, member_name);
    if (member == NULL || member->type == NULL)
    {
        format_expr_error(message, sizeof message, expr,
                          "Unknown member %s", member_name);
        add_check_log(message, visitor);
        return;
    }
    /* assign_expression_type handles cloning and typedef resolution */
    assign_expression_type(expr, member->type, mean->compiler);
}

static void enter_commaexpr(Expression *expr, Visitor *visitor) {}
static void leave_commaexpr(Expression *expr, Visitor *visitor)
{
    Expression *right = expr->u.comma_expression.right;
    if (right->type)
    {
        expr->type = right->type;
    }
    else
    {
        char message[256];
        format_expr_error(message, sizeof message, expr,
                          "Cannot find comma right type");
        add_check_log(message, visitor);
    }
}

MeanVisitor *create_mean_visitor(CS_Compiler *compiler)
{
    if (compiler == NULL)
    {
        fprintf(stderr, "Compiler is NULL\n");
        exit(1);
    }

    MeanVisitor *visitor = calloc(1, sizeof(MeanVisitor));
    visitor->check_log = NULL;
    visitor->check_log_tail = NULL;
    visitor->log_count = 0;
    visitor->current_scope = NULL;
    visitor->compiler = compiler;

    /* Legacy function pointer arrays are no longer used.
     * Switch-based traversal (mean_traverse_expr/stmt) is used instead. */

    return visitor;
}

void mean_visitor_enter_function(MeanVisitor *visitor, FunctionDeclaration *func)
{
    push_scope(visitor);
    visitor->current_function = func;

    /* Resolve return type for builtin types */
    if (func->parsed_type)
    {
        func->type = resolve_parsed_type(visitor->compiler, func->parsed_type);
    }
    else if (func->type)
    {
        /* Type is already resolved, just copy */
        func->type = cs_copy_type_specifier(func->type);
    }

    ParameterList *param = func->param;
    int param_index = 0;
    while (param)
    {
        if (param->is_ellipsis)
        {
            break;
        }
        if (param->parsed_type)
        {
            param->type = resolve_parsed_type(visitor->compiler, param->parsed_type);
        }
        Declaration *decl = calloc(1, sizeof(Declaration));
        decl->name = param->name;
        decl->type = param->type;
        decl->parsed_type = param->parsed_type;
        decl->initializer = NULL;
        decl->index = param_index;
        /* long/double use 2 slots on JVM */
        if (cs_type_is_long_exact(param->type) || cs_type_is_double_exact(param->type))
        {
            param_index = param_index + 2;
        }
        else
        {
            param_index = param_index + 1;
        }
        decl->needs_heap_lift = false;
        /* Resolve typedef names for parameter types */
        finalize_declaration_type(decl, visitor->compiler);
        /* Also update param->type so it's available for code generation */
        param->type = decl->type;
        /* Link declaration to parameter for codegen heap-lift handling */
        param->decl = decl;
        add_decl_to_scope(visitor, decl);
        param = param->next;
    }
}

void mean_visitor_leave_function(MeanVisitor *visitor)
{
    pop_scope(visitor);
    visitor->current_function = NULL;
}

/* Switch-based AST traversal for MeanVisitor.
 * These functions replace function pointer dispatch with direct switch-case,
 * preparing for self-compilation to JVM (no function pointers needed). */

void mean_traverse_expr(Expression *expr, MeanVisitor *visitor);
void mean_traverse_stmt(Statement *stmt, MeanVisitor *visitor);

static void mean_enter_expr(Expression *expr, MeanVisitor *visitor)
{
    switch (expr->kind)
    {
    case INT_EXPRESSION:
        enter_intexpr(expr, (Visitor *)visitor);
        break;
    case UINT_EXPRESSION:
        enter_uintexpr(expr, (Visitor *)visitor);
        break;
    case LONG_EXPRESSION:
        enter_longexpr(expr, (Visitor *)visitor);
        break;
    case ULONG_EXPRESSION:
        enter_ulongexpr(expr, (Visitor *)visitor);
        break;
    case BOOL_EXPRESSION:
        enter_boolexpr(expr, (Visitor *)visitor);
        break;
    case NULL_EXPRESSION:
        enter_nullexpr(expr, (Visitor *)visitor);
        break;
    case DOUBLE_EXPRESSION:
        enter_doubleexpr(expr, (Visitor *)visitor);
        break;
    case FLOAT_EXPRESSION:
        enter_floatexpr(expr, (Visitor *)visitor);
        break;
    case IDENTIFIER_EXPRESSION:
        enter_identexpr(expr, (Visitor *)visitor);
        break;
    case ADD_EXPRESSION:
        enter_addexpr(expr, (Visitor *)visitor);
        break;
    case SUB_EXPRESSION:
        enter_subexpr(expr, (Visitor *)visitor);
        break;
    case MUL_EXPRESSION:
        enter_mulexpr(expr, (Visitor *)visitor);
        break;
    case DIV_EXPRESSION:
        enter_divexpr(expr, (Visitor *)visitor);
        break;
    case MOD_EXPRESSION:
        enter_modexpr(expr, (Visitor *)visitor);
        break;
    case GT_EXPRESSION:
        enter_gtexpr(expr, (Visitor *)visitor);
        break;
    case GE_EXPRESSION:
        enter_geexpr(expr, (Visitor *)visitor);
        break;
    case LT_EXPRESSION:
        enter_ltexpr(expr, (Visitor *)visitor);
        break;
    case LE_EXPRESSION:
        enter_leexpr(expr, (Visitor *)visitor);
        break;
    case EQ_EXPRESSION:
        enter_eqexpr(expr, (Visitor *)visitor);
        break;
    case NE_EXPRESSION:
        enter_neexpr(expr, (Visitor *)visitor);
        break;
    case LOGICAL_AND_EXPRESSION:
        enter_landexpr(expr, (Visitor *)visitor);
        break;
    case LOGICAL_OR_EXPRESSION:
        enter_lorexpr(expr, (Visitor *)visitor);
        break;
    case INCREMENT_EXPRESSION:
        enter_incexpr(expr, (Visitor *)visitor);
        break;
    case DECREMENT_EXPRESSION:
        enter_decexpr(expr, (Visitor *)visitor);
        break;
    case MINUS_EXPRESSION:
        enter_minusexpr(expr, (Visitor *)visitor);
        break;
    case PLUS_EXPRESSION:
        enter_plusexpr(expr, (Visitor *)visitor);
        break;
    case LOGICAL_NOT_EXPRESSION:
        enter_lognotexpr(expr, (Visitor *)visitor);
        break;
    case ASSIGN_EXPRESSION:
        enter_assignexpr(expr, (Visitor *)visitor);
        break;
    case FUNCTION_CALL_EXPRESSION:
        enter_funccallexpr(expr, (Visitor *)visitor);
        break;
    case CAST_EXPRESSION:
        enter_castexpr(expr, (Visitor *)visitor);
        break;
    case STRING_EXPRESSION:
        enter_stringexpr(expr, (Visitor *)visitor);
        break;
    case INITIALIZER_LIST_EXPRESSION:
    case DESIGNATED_INITIALIZER_EXPRESSION:
    case ARRAY_TO_POINTER_EXPRESSION:
        enter_generic_expr(expr, (Visitor *)visitor);
        break;
    case BIT_NOT_EXPRESSION:
        enter_bitnotexpr(expr, (Visitor *)visitor);
        break;
    case ADDRESS_EXPRESSION:
        enter_addrexpr(expr, (Visitor *)visitor);
        break;
    case DEREFERENCE_EXPRESSION:
        enter_derefexpr(expr, (Visitor *)visitor);
        break;
    case SIZEOF_EXPRESSION:
        enter_sizeofexpr(expr, (Visitor *)visitor);
        break;
    case LSHIFT_EXPRESSION:
        enter_lshiftexpr(expr, (Visitor *)visitor);
        break;
    case RSHIFT_EXPRESSION:
        enter_rshiftexpr(expr, (Visitor *)visitor);
        break;
    case BIT_AND_EXPRESSION:
        enter_bitandexpr(expr, (Visitor *)visitor);
        break;
    case BIT_XOR_EXPRESSION:
        enter_bitxorexpr(expr, (Visitor *)visitor);
        break;
    case BIT_OR_EXPRESSION:
        enter_bitorexpr(expr, (Visitor *)visitor);
        break;
    case TYPE_CAST_EXPRESSION:
        enter_typecastexpr(expr, (Visitor *)visitor);
        break;
    case ARRAY_EXPRESSION:
        enter_arrayexpr(expr, (Visitor *)visitor);
        break;
    case MEMBER_EXPRESSION:
        enter_memberexpr(expr, (Visitor *)visitor);
        break;
    case CONDITIONAL_EXPRESSION:
        enter_conditionalexpr(expr, (Visitor *)visitor);
        break;
    case COMMA_EXPRESSION:
        enter_commaexpr(expr, (Visitor *)visitor);
        break;
    default:
        DBG_PRINT("mean_enter_expr: unhandled kind %d\n", expr->kind);
        break;
    }
}

static void mean_leave_expr(Expression *expr, MeanVisitor *visitor)
{
    switch (expr->kind)
    {
    case INT_EXPRESSION:
        leave_intexpr(expr, (Visitor *)visitor);
        break;
    case UINT_EXPRESSION:
        leave_uintexpr(expr, (Visitor *)visitor);
        break;
    case LONG_EXPRESSION:
        leave_longexpr(expr, (Visitor *)visitor);
        break;
    case ULONG_EXPRESSION:
        leave_ulongexpr(expr, (Visitor *)visitor);
        break;
    case BOOL_EXPRESSION:
        leave_boolexpr(expr, (Visitor *)visitor);
        break;
    case NULL_EXPRESSION:
        leave_nullexpr(expr, (Visitor *)visitor);
        break;
    case DOUBLE_EXPRESSION:
        leave_doubleexpr(expr, (Visitor *)visitor);
        break;
    case FLOAT_EXPRESSION:
        leave_floatexpr(expr, (Visitor *)visitor);
        break;
    case IDENTIFIER_EXPRESSION:
        leave_identexpr(expr, (Visitor *)visitor);
        break;
    case ADD_EXPRESSION:
        leave_addexpr(expr, (Visitor *)visitor);
        break;
    case SUB_EXPRESSION:
        leave_subexpr(expr, (Visitor *)visitor);
        break;
    case MUL_EXPRESSION:
        leave_mulexpr(expr, (Visitor *)visitor);
        break;
    case DIV_EXPRESSION:
        leave_divexpr(expr, (Visitor *)visitor);
        break;
    case MOD_EXPRESSION:
        leave_modexpr(expr, (Visitor *)visitor);
        break;
    case GT_EXPRESSION:
        leave_gtexpr(expr, (Visitor *)visitor);
        break;
    case GE_EXPRESSION:
        leave_geexpr(expr, (Visitor *)visitor);
        break;
    case LT_EXPRESSION:
        leave_ltexpr(expr, (Visitor *)visitor);
        break;
    case LE_EXPRESSION:
        leave_leexpr(expr, (Visitor *)visitor);
        break;
    case EQ_EXPRESSION:
        leave_eqexpr(expr, (Visitor *)visitor);
        break;
    case NE_EXPRESSION:
        leave_neexpr(expr, (Visitor *)visitor);
        break;
    case LOGICAL_AND_EXPRESSION:
        leave_landexpr(expr, (Visitor *)visitor);
        break;
    case LOGICAL_OR_EXPRESSION:
        leave_lorexpr(expr, (Visitor *)visitor);
        break;
    case INCREMENT_EXPRESSION:
        leave_incexpr(expr, (Visitor *)visitor);
        break;
    case DECREMENT_EXPRESSION:
        leave_decexpr(expr, (Visitor *)visitor);
        break;
    case MINUS_EXPRESSION:
        leave_minusexpr(expr, (Visitor *)visitor);
        break;
    case PLUS_EXPRESSION:
        leave_plusexpr(expr, (Visitor *)visitor);
        break;
    case LOGICAL_NOT_EXPRESSION:
        leave_lognotexpr(expr, (Visitor *)visitor);
        break;
    case ASSIGN_EXPRESSION:
        leave_assignexpr(expr, (Visitor *)visitor);
        break;
    case FUNCTION_CALL_EXPRESSION:
        leave_funccallexpr(expr, (Visitor *)visitor);
        break;
    case CAST_EXPRESSION:
        leave_castexpr(expr, (Visitor *)visitor);
        break;
    case STRING_EXPRESSION:
        leave_stringexpr(expr, (Visitor *)visitor);
        break;
    case INITIALIZER_LIST_EXPRESSION:
    case DESIGNATED_INITIALIZER_EXPRESSION:
    case ARRAY_TO_POINTER_EXPRESSION:
        leave_generic_expr(expr, (Visitor *)visitor);
        break;
    case BIT_NOT_EXPRESSION:
        leave_bitnotexpr(expr, (Visitor *)visitor);
        break;
    case ADDRESS_EXPRESSION:
        leave_addrexpr(expr, (Visitor *)visitor);
        break;
    case DEREFERENCE_EXPRESSION:
        leave_derefexpr(expr, (Visitor *)visitor);
        break;
    case SIZEOF_EXPRESSION:
        leave_sizeofexpr(expr, (Visitor *)visitor);
        break;
    case LSHIFT_EXPRESSION:
        leave_lshiftexpr(expr, (Visitor *)visitor);
        break;
    case RSHIFT_EXPRESSION:
        leave_rshiftexpr(expr, (Visitor *)visitor);
        break;
    case BIT_AND_EXPRESSION:
        leave_bitandexpr(expr, (Visitor *)visitor);
        break;
    case BIT_XOR_EXPRESSION:
        leave_bitxorexpr(expr, (Visitor *)visitor);
        break;
    case BIT_OR_EXPRESSION:
        leave_bitorexpr(expr, (Visitor *)visitor);
        break;
    case TYPE_CAST_EXPRESSION:
        leave_typecastexpr(expr, (Visitor *)visitor);
        break;
    case ARRAY_EXPRESSION:
        leave_arrayexpr(expr, (Visitor *)visitor);
        break;
    case MEMBER_EXPRESSION:
        leave_memberexpr(expr, (Visitor *)visitor);
        break;
    case CONDITIONAL_EXPRESSION:
        leave_conditionalexpr(expr, (Visitor *)visitor);
        break;
    case COMMA_EXPRESSION:
        leave_commaexpr(expr, (Visitor *)visitor);
        break;
    default:
        DBG_PRINT("mean_leave_expr: unhandled kind %d\n", expr->kind);
        break;
    }
}

static void mean_traverse_expr_children(Expression *expr, MeanVisitor *visitor)
{
    switch (expr->kind)
    {
    case STRING_EXPRESSION:
    case IDENTIFIER_EXPRESSION:
    case DOUBLE_EXPRESSION:
    case FLOAT_EXPRESSION:
    case LONG_EXPRESSION:
    case ULONG_EXPRESSION:
    case INT_EXPRESSION:
    case UINT_EXPRESSION:
    case BOOL_EXPRESSION:
        break;
    case ARRAY_EXPRESSION:
        mean_traverse_expr(expr->u.array_expression.array, visitor);
        mean_traverse_expr(expr->u.array_expression.index, visitor);
        break;
    case MEMBER_EXPRESSION:
        mean_traverse_expr(expr->u.member_expression.target, visitor);
        break;
    case CONDITIONAL_EXPRESSION:
        mean_traverse_expr(expr->u.conditional_expression.condition, visitor);
        mean_traverse_expr(expr->u.conditional_expression.true_expr, visitor);
        mean_traverse_expr(expr->u.conditional_expression.false_expr, visitor);
        break;
    case COMMA_EXPRESSION:
        mean_traverse_expr(expr->u.comma_expression.left, visitor);
        mean_traverse_expr(expr->u.comma_expression.right, visitor);
        break;
    case INITIALIZER_LIST_EXPRESSION:
    {
        for (ExpressionList *p = expr->u.initializer_list; p; p = p->next)
        {
            mean_traverse_expr(p->expression, visitor);
        }
        break;
    }
    case DESIGNATED_INITIALIZER_EXPRESSION:
        mean_traverse_expr(expr->u.designated_initializer.value, visitor);
        break;
    case INCREMENT_EXPRESSION:
    case DECREMENT_EXPRESSION:
        mean_traverse_expr(expr->u.inc_dec.target, visitor);
        break;
    case MINUS_EXPRESSION:
        mean_traverse_expr(expr->u.minus_expression, visitor);
        break;
    case PLUS_EXPRESSION:
        mean_traverse_expr(expr->u.plus_expression, visitor);
        break;
    case LOGICAL_NOT_EXPRESSION:
        mean_traverse_expr(expr->u.logical_not_expression, visitor);
        break;
    case BIT_NOT_EXPRESSION:
        mean_traverse_expr(expr->u.bit_not_expression, visitor);
        break;
    case ADDRESS_EXPRESSION:
        mean_traverse_expr(expr->u.address_expression, visitor);
        break;
    case DEREFERENCE_EXPRESSION:
        mean_traverse_expr(expr->u.dereference_expression, visitor);
        break;
    case ASSIGN_EXPRESSION:
        mean_traverse_expr(expr->u.assignment_expression.left, visitor);
        /* No notify handler needed for mean visitor */
        mean_traverse_expr(expr->u.assignment_expression.right, visitor);
        break;
    case CAST_EXPRESSION:
        mean_traverse_expr(expr->u.cast_expression.expr, visitor);
        break;
    case TYPE_CAST_EXPRESSION:
        mean_traverse_expr(expr->u.type_cast_expression.expr, visitor);
        break;
    case SIZEOF_EXPRESSION:
        if (!expr->u.sizeof_expression.is_type)
        {
            mean_traverse_expr(expr->u.sizeof_expression.expr, visitor);
        }
        break;
    case ARRAY_TO_POINTER_EXPRESSION:
        mean_traverse_expr(expr->u.array_to_pointer, visitor);
        break;
    case FUNCTION_CALL_EXPRESSION:
    {
        ArgumentList *args = expr->u.function_call_expression.argument;
        for (; args; args = args->next)
        {
            mean_traverse_expr(args->expr, visitor);
        }
        mean_traverse_expr(expr->u.function_call_expression.function, visitor);
        break;
    }
    case LOGICAL_AND_EXPRESSION:
    case LOGICAL_OR_EXPRESSION:
    case LT_EXPRESSION:
    case LE_EXPRESSION:
    case GT_EXPRESSION:
    case GE_EXPRESSION:
    case EQ_EXPRESSION:
    case NE_EXPRESSION:
    case LSHIFT_EXPRESSION:
    case RSHIFT_EXPRESSION:
    case BIT_AND_EXPRESSION:
    case BIT_XOR_EXPRESSION:
    case BIT_OR_EXPRESSION:
    case MOD_EXPRESSION:
    case DIV_EXPRESSION:
    case MUL_EXPRESSION:
    case SUB_EXPRESSION:
    case ADD_EXPRESSION:
        if (expr->u.binary_expression.left)
        {
            mean_traverse_expr(expr->u.binary_expression.left, visitor);
        }
        if (expr->u.binary_expression.right)
        {
            mean_traverse_expr(expr->u.binary_expression.right, visitor);
        }
        break;
    default:
        DBG_PRINT("mean_traverse_expr_children: unhandled kind %d\n", expr->kind);
        break;
    }
}

void mean_traverse_expr(Expression *expr, MeanVisitor *visitor)
{
    if (expr)
    {
        mean_enter_expr(expr, visitor);
        mean_traverse_expr_children(expr, visitor);
        mean_leave_expr(expr, visitor);
    }
}

static void mean_enter_stmt(Statement *stmt, MeanVisitor *visitor)
{
    switch (stmt->type)
    {
    case EXPRESSION_STATEMENT:
        enter_exprstmt(stmt, (Visitor *)visitor);
        break;
    case DECLARATION_STATEMENT:
        enter_declstmt(stmt, (Visitor *)visitor);
        break;
    case COMPOUND_STATEMENT:
        enter_compoundstmt(stmt, (Visitor *)visitor);
        break;
    case IF_STATEMENT:
    case WHILE_STATEMENT:
    case DO_WHILE_STATEMENT:
    case FOR_STATEMENT:
        enter_generic_stmt(stmt, (Visitor *)visitor);
        break;
    case SWITCH_STATEMENT:
        enter_switchstmt(stmt, (Visitor *)visitor);
        break;
    case CASE_STATEMENT:
    case DEFAULT_STATEMENT:
    case GOTO_STATEMENT:
    case LABEL_STATEMENT:
    case BREAK_STATEMENT:
    case CONTINUE_STATEMENT:
    case RETURN_STATEMENT:
        enter_generic_stmt(stmt, (Visitor *)visitor);
        break;
    default:
        DBG_PRINT("mean_enter_stmt: unhandled type %d\n", stmt->type);
        break;
    }
}

static void mean_leave_stmt(Statement *stmt, MeanVisitor *visitor)
{
    switch (stmt->type)
    {
    case EXPRESSION_STATEMENT:
        leave_exprstmt(stmt, (Visitor *)visitor);
        break;
    case DECLARATION_STATEMENT:
        leave_declstmt(stmt, (Visitor *)visitor);
        break;
    case COMPOUND_STATEMENT:
        leave_compoundstmt(stmt, (Visitor *)visitor);
        break;
    case IF_STATEMENT:
    case WHILE_STATEMENT:
    case DO_WHILE_STATEMENT:
    case FOR_STATEMENT:
        leave_generic_stmt(stmt, (Visitor *)visitor);
        break;
    case SWITCH_STATEMENT:
        leave_switchstmt(stmt, (Visitor *)visitor);
        break;
    case CASE_STATEMENT:
        leave_casestmt(stmt, (Visitor *)visitor);
        break;
    case DEFAULT_STATEMENT:
    case GOTO_STATEMENT:
    case LABEL_STATEMENT:
    case BREAK_STATEMENT:
    case CONTINUE_STATEMENT:
        leave_generic_stmt(stmt, (Visitor *)visitor);
        break;
    case RETURN_STATEMENT:
    {
        /* Propagate return type to NULL expressions for proper JVM bytecode generation */
        Expression *ret_expr = stmt->u.return_s.expression;
        TypeSpecifier *return_type = visitor->current_function
                                         ? visitor->current_function->type
                                         : NULL;
        if (ret_expr && return_type && cs_type_is_pointer(return_type))
        {
            /* If expression is void* (NULL), set its type to the actual return type */
            if (ret_expr->type && is_void_pointer(ret_expr->type))
            {
                ret_expr->type = return_type;
            }
        }
        break;
    }
    default:
        DBG_PRINT("mean_leave_stmt: unhandled type %d\n", stmt->type);
        break;
    }
}

static void mean_traverse_stmt_children(Statement *stmt, MeanVisitor *visitor)
{
    switch (stmt->type)
    {
    case EXPRESSION_STATEMENT:
        mean_traverse_expr(stmt->u.expression_s, visitor);
        break;
    case DECLARATION_STATEMENT:
    {
        /* VLA size expressions are now handled in leave_declstmt after type is finalized */
        Declaration *decl = stmt->u.declaration_s;
        mean_traverse_expr(decl ? decl->initializer : NULL, visitor);
        break;
    }
    case COMPOUND_STATEMENT:
    {
        for (StatementList *p = stmt->u.compound_s.list; p; p = p->next)
        {
            mean_traverse_stmt(p->stmt, visitor);
        }
        break;
    }
    case IF_STATEMENT:
        mean_traverse_expr(stmt->u.if_s.condition, visitor);
        mean_traverse_stmt(stmt->u.if_s.then_statement, visitor);
        mean_traverse_stmt(stmt->u.if_s.else_statement, visitor);
        break;
    case WHILE_STATEMENT:
        mean_traverse_expr(stmt->u.while_s.condition, visitor);
        mean_traverse_stmt(stmt->u.while_s.body, visitor);
        break;
    case DO_WHILE_STATEMENT:
        mean_traverse_stmt(stmt->u.do_s.body, visitor);
        mean_traverse_expr(stmt->u.do_s.condition, visitor);
        break;
    case FOR_STATEMENT:
        mean_traverse_stmt(stmt->u.for_s.init, visitor);
        mean_traverse_expr(stmt->u.for_s.condition, visitor);
        mean_traverse_stmt(stmt->u.for_s.body, visitor);
        mean_traverse_expr(stmt->u.for_s.post, visitor);
        break;
    case SWITCH_STATEMENT:
        mean_traverse_expr(stmt->u.switch_s.expression, visitor);
        /* Push switch expression type for case label checking */
        if (stmt->u.switch_s.expression)
        {
            push_switch_type(visitor, stmt->u.switch_s.expression->type);
        }
        mean_traverse_stmt(stmt->u.switch_s.body, visitor);
        break;
    case CASE_STATEMENT:
        mean_traverse_expr(stmt->u.case_s.expression, visitor);
        mean_traverse_stmt(stmt->u.case_s.statement, visitor);
        break;
    case DEFAULT_STATEMENT:
        mean_traverse_stmt(stmt->u.default_s.statement, visitor);
        break;
    case LABEL_STATEMENT:
        mean_traverse_stmt(stmt->u.label_s.statement, visitor);
        break;
    case RETURN_STATEMENT:
        mean_traverse_expr(stmt->u.return_s.expression, visitor);
        break;
    case GOTO_STATEMENT:
    case BREAK_STATEMENT:
    case CONTINUE_STATEMENT:
        break;
    default:
        DBG_PRINT("mean_traverse_stmt_children: unhandled type %d\n", stmt->type);
        break;
    }
}

void mean_traverse_stmt(Statement *stmt, MeanVisitor *visitor)
{
    if (stmt)
    {
        mean_enter_stmt(stmt, visitor);
        mean_traverse_stmt_children(stmt, visitor);
        mean_leave_stmt(stmt, visitor);
    }
}
