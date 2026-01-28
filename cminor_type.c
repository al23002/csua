/*
 * Cminor Type System
 *
 * Pure C type system operations. No JVM dependencies.
 * JVM-specific type operations are in codegen_jvm_types.c
 */

#include "cminor_type.h"
#include "create.h"
#include "parsed_type.h"
#include "header_store.h"
#include "header_index.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ============================================================
 * Basic Type Access
 * ============================================================ */

/* Get the basic type of a scalar type (CS_TYPE_BASIC or CS_TYPE_NAMED).
 * Returns CS_BASIC_TYPE_PLUS_ONE for non-scalar types (arrays, pointers).
 * Does NOT walk to child - checks this type node directly. */
CS_BasicType cs_type_basic_type(TypeSpecifier *type)
{
    if (!type)
    {
        fprintf(stderr, "cs_type_basic_type: type is NULL\n");
        exit(1);
    }
    switch (type->kind)
    {
    case CS_TYPE_BASIC:
        return type->u.basic.basic_type;
    case CS_TYPE_NAMED:
        return type->u.named.basic_type;
    default:
        return CS_BASIC_TYPE_PLUS_ONE;
    }
}

/* ============================================================
 * String Conversion Helpers
 * ============================================================ */

static const char *basic_type_name(CS_BasicType type)
{
    switch (type)
    {
    case CS_VOID_TYPE:
        return "void";
    case CS_CHAR_TYPE:
        return "char";
    case CS_SHORT_TYPE:
        return "short";
    case CS_BOOLEAN_TYPE:
        return "bool";
    case CS_INT_TYPE:
        return "int";
    case CS_LONG_TYPE:
        return "long";
    case CS_FLOAT_TYPE:
        return "float";
    case CS_DOUBLE_TYPE:
        return "double";
    case CS_STRUCT_TYPE:
        return "struct";
    case CS_UNION_TYPE:
        return "union";
    case CS_ENUM_TYPE:
        return "enum";
    case CS_TYPEDEF_NAME:
        return "typedef";
    default:
        return "<unknown>";
    }
}

static void append_token(char *buffer, int buffer_size, const char *token)
{
    if (!buffer || buffer_size == 0 || !token)
    {
        return;
    }

    int existing = strlen(buffer);
    if (existing >= buffer_size - 1)
    {
        buffer[buffer_size - 1] = '\0';
        return;
    }

    int remaining = buffer_size - existing - 1;
    int copy = strlen(token);
    if (copy > remaining)
    {
        copy = remaining;
    }

    strncpy(buffer + existing, token, copy);
    buffer[existing + copy] = '\0';
}

/* ============================================================
 * Type Tree Navigation
 * ============================================================ */

TypeSpecifier *cs_type_child(TypeSpecifier *type)
{
    if (!type)
    {
        return NULL;
    }
    return type->child;
}

bool cs_type_named_id_equals(TypeSpecifier *a, TypeSpecifier *b)
{
    if (!a || !b)
    {
        return a == b;
    }
    if (a->kind != CS_TYPE_NAMED || b->kind != CS_TYPE_NAMED)
    {
        return false;
    }
    /* Compare TypeIdentity directly without taking address */
    char *a_name = a->u.named.id.name;
    char *b_name = b->u.named.id.name;
    if (!a_name || !b_name)
    {
        return a_name == b_name;
    }
    return strcmp(a_name, b_name) == 0;
}

/* Check if a TypeIdentity represents an anonymous type.
 * Anonymous types have names containing '$' (e.g., "Foo$0"). */
bool cs_type_identity_is_anonymous(TypeIdentity *id)
{
    if (!id || !id->name)
    {
        return false;
    }
    return strchr(id->name, '$') != NULL;
}

const char *cs_type_user_type_name(TypeSpecifier *type)
{
    if (!type || type->kind != CS_TYPE_NAMED)
    {
        return NULL;
    }
    return type->u.named.id.name;
}

void cs_type_set_user_type_name(TypeSpecifier *type, const char *name)
{
    if (!type || type->kind != CS_TYPE_NAMED)
    {
        return;
    }
    type->u.named.id.name = (char *)name;
}

/* ============================================================
 * Struct Member Access
 * ============================================================ */

StructMember *cs_type_struct_members(TypeSpecifier *type)
{
    if (!type)
    {
        return NULL;
    }
    switch (type->kind)
    {
    case CS_TYPE_BASIC:
        return type->u.basic.struct_members;
    case CS_TYPE_NAMED:
        return type->u.named.struct_members;
    default:
        return NULL;
    }
}

void cs_type_set_struct_members(TypeSpecifier *type, StructMember *members)
{
    if (!type)
    {
        return;
    }
    switch (type->kind)
    {
    case CS_TYPE_BASIC:
        type->u.basic.struct_members = members;
        break;
    case CS_TYPE_NAMED:
        type->u.named.struct_members = members;
        break;
    default:
        break;
    }
}

/* ============================================================
 * Array Size Access
 * ============================================================ */

Expression *cs_type_array_size(TypeSpecifier *type)
{
    if (!type || type->kind != CS_TYPE_ARRAY)
    {
        return NULL;
    }
    return type->u.array.array_size;
}

void cs_type_set_array_size(TypeSpecifier *type, Expression *array_size)
{
    if (!type || type->kind != CS_TYPE_ARRAY)
    {
        return;
    }
    type->u.array.array_size = array_size;
}

/* ============================================================
 * Type Info (Direct Check - does NOT traverse)
 * ============================================================ */

CS_TypeInfo cs_type_info(TypeSpecifier *type)
{
    CS_TypeInfo info;
    info.type = NULL;
    info.category = CS_TYPE_CATEGORY_INVALID;

    if (!type)
    {
        return info;
    }

    info.type = type;

    switch (type->kind)
    {
    case CS_TYPE_BASIC:
        info.category = CS_TYPE_CATEGORY_BASIC;
        break;
    case CS_TYPE_POINTER:
        info.category = CS_TYPE_CATEGORY_POINTER;
        break;
    case CS_TYPE_ARRAY:
        info.category = CS_TYPE_CATEGORY_ARRAY;
        break;
    case CS_TYPE_NAMED:
        info.category = CS_TYPE_CATEGORY_NAMED;
        break;
    default:
        info.category = CS_TYPE_CATEGORY_INVALID;
        break;
    }

    return info;
}

/* ============================================================
 * Reference Type Helpers
 * ============================================================ */

TypeSpecifier *cs_type_box_reference(TypeSpecifier *value_type)
{
    if (!value_type)
    {
        return NULL;
    }

    TypeSpecifier *boxed_value = cs_copy_type_specifier(value_type);
    return cs_wrap_pointer(boxed_value, 1);
}

TypeSpecifier *cs_type_reference_for_address(TypeSpecifier *value_type,
                                             bool *requires_heap_lift)
{
    if (requires_heap_lift)
    {
        *requires_heap_lift = false;
    }

    if (!value_type)
    {
        return NULL;
    }

    CS_TypeInfo info = cs_type_info(value_type);

    /* For primitives or pointers, we need heap lift to get a reference */
    if (info.category == CS_TYPE_CATEGORY_BASIC ||
        info.category == CS_TYPE_CATEGORY_POINTER)
    {
        if (requires_heap_lift)
        {
            *requires_heap_lift = true;
        }
        return cs_type_box_reference(value_type);
    }

    /* For named types (structs), we also need heap lift.
     * Although structs are reference types in JVM, &struct_var still needs
     * the variable itself to be in a box, so that *ptr = new_struct works. */
    if (info.category == CS_TYPE_CATEGORY_NAMED)
    {
        if (requires_heap_lift)
        {
            *requires_heap_lift = true;
        }
        TypeSpecifier *copy = cs_copy_type_specifier(value_type);
        return cs_wrap_pointer(copy, 1);
    }

    /* For arrays, we also need heap lift to get a reference to the array variable.
     * In Cminor, int a[] is a Java array reference, so &a needs to box the
     * array variable itself (not the array contents). */
    if (info.category == CS_TYPE_CATEGORY_ARRAY)
    {
        if (requires_heap_lift)
        {
            *requires_heap_lift = true;
        }
        TypeSpecifier *copy = cs_copy_type_specifier(value_type);
        return cs_wrap_pointer(copy, 1);
    }

    /* Fallback: just copy */
    return cs_copy_type_specifier(value_type);
}

/* ============================================================
 * Basic Type Predicates
 * ============================================================ */

/* Check if this type node (not walking to deepest child) has the given basic type.
 * Returns false for POINTER and ARRAY types - they don't have basic types.
 * This design prevents accidental confusion like has_basic_type(int*, INT) == true.
 */
static bool has_basic_type(TypeSpecifier *type, CS_BasicType basic)
{
    if (!type)
    {
        return false;
    }
    /* POINTER and ARRAY don't have basic types at this node */
    if (type->kind == CS_TYPE_POINTER || type->kind == CS_TYPE_ARRAY)
    {
        return false;
    }
    /* For BASIC and NAMED, check the basic_type directly */
    return cs_type_basic_type(type) == basic;
}

/* ── Scalar Type Queries ── */

/* Exact type queries (for Java-style type promotion) */
bool cs_type_is_char_exact(TypeSpecifier *type)
{
    return has_basic_type(type, CS_CHAR_TYPE);
}

bool cs_type_is_short_exact(TypeSpecifier *type)
{
    return has_basic_type(type, CS_SHORT_TYPE);
}

bool cs_type_is_int_exact(TypeSpecifier *type)
{
    return has_basic_type(type, CS_INT_TYPE);
}

bool cs_type_is_enum(TypeSpecifier *type)
{
    return has_basic_type(type, CS_ENUM_TYPE);
}

bool cs_type_is_named_enum(TypeSpecifier *type)
{
    if (!type)
        return false;
    /* Named enum uses CS_TYPE_NAMED with enum basic type */
    if (type->kind == CS_TYPE_NAMED &&
        type->u.named.basic_type == CS_ENUM_TYPE &&
        type->u.named.id.name != NULL)
    {
        return true;
    }
    return false;
}

bool cs_type_is_long_exact(TypeSpecifier *type)
{
    return has_basic_type(type, CS_LONG_TYPE);
}

bool cs_type_is_float_exact(TypeSpecifier *type)
{
    return has_basic_type(type, CS_FLOAT_TYPE);
}

bool cs_type_is_double_exact(TypeSpecifier *type)
{
    return has_basic_type(type, CS_DOUBLE_TYPE);
}

/* Numeric type category queries */
bool cs_type_is_integral(TypeSpecifier *type)
{
    return cs_type_is_char_exact(type) ||
           cs_type_is_short_exact(type) ||
           cs_type_is_int_exact(type) ||
           cs_type_is_long_exact(type);
}

bool cs_type_is_floating(TypeSpecifier *type)
{
    return cs_type_is_float_exact(type) ||
           cs_type_is_double_exact(type);
}

bool cs_type_is_numeric(TypeSpecifier *type)
{
    return cs_type_is_integral(type) || cs_type_is_floating(type);
}

/* Java Binary Numeric Promotion (JLS 5.6.2)
 * Returns the result type for binary operations on two numeric types. */
CS_BasicType cs_type_binary_promoted(TypeSpecifier *left, TypeSpecifier *right)
{
    if (!cs_type_is_numeric(left) || !cs_type_is_numeric(right))
    {
        return CS_BASIC_TYPE_PLUS_ONE; /* invalid */
    }

    /* If either operand is double, result is double */
    if (cs_type_is_double_exact(left) || cs_type_is_double_exact(right))
    {
        return CS_DOUBLE_TYPE;
    }
    /* Else if either operand is float, result is float */
    if (cs_type_is_float_exact(left) || cs_type_is_float_exact(right))
    {
        return CS_FLOAT_TYPE;
    }
    /* Else if either operand is long, result is long */
    if (cs_type_is_long_exact(left) || cs_type_is_long_exact(right))
    {
        return CS_LONG_TYPE;
    }
    /* Otherwise (char/short/int), result is int */
    return CS_INT_TYPE;
}

/* Internal: Check if widening from source to target basic type is needed */
static bool needs_widening_to_basic(TypeSpecifier *source, CS_BasicType target)
{
    if (!cs_type_is_numeric(source))
    {
        return false;
    }

    /* char/short/int -> int: no JVM instruction needed (all stored as int) */
    if (target == CS_INT_TYPE)
    {
        return false;
    }

    /* Check if source is "smaller" than target */
    if (target == CS_LONG_TYPE)
    {
        return !cs_type_is_long_exact(source) && !cs_type_is_float_exact(source) &&
               !cs_type_is_double_exact(source);
    }
    if (target == CS_FLOAT_TYPE)
    {
        return !cs_type_is_float_exact(source) && !cs_type_is_double_exact(source);
    }
    if (target == CS_DOUBLE_TYPE)
    {
        return !cs_type_is_double_exact(source);
    }

    return false;
}

/* Check if widening from source to target type is needed for JVM */
bool cs_type_needs_widening_to(TypeSpecifier *source, TypeSpecifier *target)
{
    if (!source || !target)
    {
        return false;
    }
    CS_BasicType target_basic = cs_type_basic_type(target);
    return needs_widening_to_basic(source, target_basic);
}

/* Internal: Get widening cast type from source to target basic type */
static CS_CastType widening_cast_to_basic(TypeSpecifier *source, CS_BasicType target)
{
    if (!cs_type_is_numeric(source))
    {
        return 0;
    }

    /* char/short -> int: need zero-extend for unsigned types */
    if (target == CS_INT_TYPE)
    {
        if (cs_type_is_char_exact(source))
            return cs_type_is_unsigned(source) ? CS_UCHAR_TO_INT : CS_CHAR_TO_INT;
        if (cs_type_is_short_exact(source))
            return cs_type_is_unsigned(source) ? CS_USHORT_TO_INT : CS_SHORT_TO_INT;
        return 0; /* int -> int: no cast */
    }

    /* -> long: char/short/int all need i2l (or zero-extend for unsigned) */
    if (target == CS_LONG_TYPE)
    {
        if (cs_type_is_long_exact(source))
            return 0;
        if (cs_type_is_float_exact(source))
            return CS_FLOAT_TO_LONG;
        if (cs_type_is_double_exact(source))
            return CS_DOUBLE_TO_LONG;
        /* char/short/int -> long: use zero-extend for unsigned */
        if (cs_type_is_unsigned(source))
            return CS_UINT_TO_ULONG;
        return CS_INT_TO_LONG;
    }

    /* -> float */
    if (target == CS_FLOAT_TYPE)
    {
        if (cs_type_is_float_exact(source))
            return 0;
        if (cs_type_is_double_exact(source))
            return CS_DOUBLE_TO_FLOAT;
        if (cs_type_is_long_exact(source))
            return CS_LONG_TO_FLOAT;
        /* char/short/int -> float */
        return CS_INT_TO_FLOAT;
    }

    /* -> double */
    if (target == CS_DOUBLE_TYPE)
    {
        if (cs_type_is_double_exact(source))
            return 0;
        if (cs_type_is_float_exact(source))
            return CS_FLOAT_TO_DOUBLE;
        if (cs_type_is_long_exact(source))
            return CS_LONG_TO_DOUBLE;
        /* char/short/int -> double */
        return CS_INT_TO_DOUBLE;
    }

    return 0;
}

/* Get widening cast type from source to target type */
CS_CastType cs_type_widening_cast_to(TypeSpecifier *source, TypeSpecifier *target)
{
    if (!source || !target)
    {
        return 0;
    }
    CS_BasicType target_basic = cs_type_basic_type(target);
    return widening_cast_to_basic(source, target_basic);
}

/* ── Step 1: Unary Integer Promotion ──
 * Small int types are promoted INDEPENDENTLY of the other operand:
 *   - signed char/short   -> int  (sign extension)
 *   - unsigned char/short -> uint (zero extension)
 * int/uint/long/ulong stay as-is.
 */
TypeSpecifier *cs_type_unary_promoted(TypeSpecifier *type)
{
    if (!type || !cs_type_is_integral(type))
    {
        return type ? cs_copy_type_specifier(type) : NULL;
    }

    /* Small int types promote to int/uint */
    if (cs_type_is_small_int(type))
    {
        TypeSpecifier *ts = cs_create_type_specifier(CS_INT_TYPE);
        cs_type_set_unsigned(ts, cs_type_is_unsigned(type));
        return ts;
    }

    /* int/uint/long/ulong stay as-is */
    return cs_copy_type_specifier(type);
}

/* ── Step 2: Binary Numeric Promotion ──
 * After unary promotion, combine two types:
 *   - float/double: standard floating point rules
 *   - int/uint/long/ulong: larger type wins, unsigned wins if same size
 */
TypeSpecifier *cs_type_binary_promoted_specifier(TypeSpecifier *left,
                                                 TypeSpecifier *right)
{
    if (!cs_type_is_numeric(left) || !cs_type_is_numeric(right))
    {
        return NULL;
    }

    /* Handle floating point */
    if (cs_type_is_double_exact(left) || cs_type_is_double_exact(right))
    {
        return cs_create_type_specifier(CS_DOUBLE_TYPE);
    }
    if (cs_type_is_float_exact(left) || cs_type_is_float_exact(right))
    {
        return cs_create_type_specifier(CS_FLOAT_TYPE);
    }

    /* Step 1: Unary promotion (small_int -> int/uint) */
    TypeSpecifier *pl = cs_type_unary_promoted(left);
    TypeSpecifier *pr = cs_type_unary_promoted(right);

    bool pl_int = cs_type_is_int_exact(pl);
    bool pr_int = cs_type_is_int_exact(pr);
    bool pl_long = cs_type_is_long_exact(pl);
    bool pr_long = cs_type_is_long_exact(pr);
    bool pl_unsigned = cs_type_is_unsigned(pl);
    bool pr_unsigned = cs_type_is_unsigned(pr);

    /* Step 2: Binary promotion */
    TypeSpecifier *result;

    /* If either is long, result is long */
    if (pl_long || pr_long)
    {
        result = cs_create_type_specifier(CS_LONG_TYPE);
    }
    else
    {
        /* Both are int/uint after unary promotion */
        result = cs_create_type_specifier(CS_INT_TYPE);
    }

    /* Signed wins: only unsigned if both operands are unsigned */
    cs_type_set_unsigned(result, pl_unsigned && pr_unsigned);

    return result;
}

/* Other scalar type queries */
bool cs_type_is_bool(TypeSpecifier *type)
{
    return has_basic_type(type, CS_BOOLEAN_TYPE);
}

bool cs_type_is_void(TypeSpecifier *type)
{
    return has_basic_type(type, CS_VOID_TYPE);
}

bool cs_type_is_void_pointer(TypeSpecifier *type)
{
    if (!cs_type_is_pointer(type))
    {
        return false;
    }
    /* Check only one level: void* is true, void** is false */
    TypeSpecifier *child = cs_type_child(type);
    if (!child)
    {
        return false;
    }
    return cs_type_is_void(child);
}

/* Check if this type is a primitive scalar type (void, char, short, int, long, float, double, bool).
 * Does NOT walk to child - checks this type node directly.
 * For arrays/pointers, returns false. Use cs_type_child() first if needed. */
bool cs_type_is_primitive(TypeSpecifier *type)
{
    if (!type)
    {
        return false;
    }
    /* Only BASIC and NAMED can be primitive */
    if (type->kind != CS_TYPE_BASIC && type->kind != CS_TYPE_NAMED)
    {
        return false;
    }
    CS_BasicType basic = cs_type_basic_type(type);
    return basic >= CS_VOID_TYPE && basic <= CS_DOUBLE_TYPE;
}

bool cs_type_is_pointer(TypeSpecifier *type)
{
    if (!type)
    {
        return false;
    }
    return type->kind == CS_TYPE_POINTER;
}

bool cs_type_is_array(TypeSpecifier *type)
{
    if (!type)
    {
        return false;
    }
    return type->kind == CS_TYPE_ARRAY;
}

bool cs_type_is_named(TypeSpecifier *type)
{
    if (!type)
    {
        return false;
    }
    return type->kind == CS_TYPE_NAMED;
}

bool cs_type_is_basic(TypeSpecifier *type)
{
    if (!type)
    {
        return false;
    }
    return type->kind == CS_TYPE_BASIC;
}

CS_TypeKind cs_type_kind(TypeSpecifier *type)
{
    if (!type)
    {
        fprintf(stderr, "cs_type_kind: type is NULL\n");
        exit(1);
    }
    return type->kind;
}

/* Compare basic types of two scalar types (with JVM-style normalization).
 * Does NOT walk to child - compares types directly.
 * Returns false if either type is not a scalar (BASIC/NAMED). */
bool cs_type_same_basic(TypeSpecifier *lhs, TypeSpecifier *rhs)
{
    if (!lhs || !rhs)
    {
        return false;
    }
    /* Only compare BASIC and NAMED types */
    if ((lhs->kind != CS_TYPE_BASIC && lhs->kind != CS_TYPE_NAMED) ||
        (rhs->kind != CS_TYPE_BASIC && rhs->kind != CS_TYPE_NAMED))
    {
        return false;
    }

    CS_BasicType lhs_basic = cs_type_basic_type(lhs);
    CS_BasicType rhs_basic = cs_type_basic_type(rhs);

    /* JVM normalization: enum and char are treated as int */
    if (lhs_basic == CS_ENUM_TYPE || lhs_basic == CS_CHAR_TYPE)
    {
        lhs_basic = CS_INT_TYPE;
    }
    if (rhs_basic == CS_ENUM_TYPE || rhs_basic == CS_CHAR_TYPE)
    {
        rhs_basic = CS_INT_TYPE;
    }

    return lhs_basic == rhs_basic;
}

/* Type equality based on C type structure.
 * Compares types recursively without walking to deepest child. */
bool cs_type_equals(TypeSpecifier *lhs, TypeSpecifier *rhs)
{
    /* Both NULL means equal (e.g., both incomplete pointer types) */
    if (!lhs && !rhs)
    {
        return true;
    }
    if (!lhs || !rhs)
    {
        return false;
    }

    /* Check kind equality */
    if (lhs->kind != rhs->kind)
    {
        return false;
    }

    switch (lhs->kind)
    {
    case CS_TYPE_BASIC:
        /* Compare basic types and signedness - int and uint are different! */
        return cs_type_basic_type(lhs) == cs_type_basic_type(rhs) &&
               lhs->is_unsigned == rhs->is_unsigned;

    case CS_TYPE_NAMED:
        /* Compare basic types and type identity */
        if (cs_type_basic_type(lhs) != cs_type_basic_type(rhs))
        {
            return false;
        }
        return cs_type_named_id_equals(lhs, rhs);

    case CS_TYPE_POINTER:
    case CS_TYPE_ARRAY:
        /* Recursively compare child types */
        return cs_type_equals(lhs->child, rhs->child);

    default:
        return false;
    }
}

bool cs_type_is_aggregate(TypeSpecifier *type)
{
    if (!type)
    {
        return false;
    }

    /* Direct check: is this type node an array, struct, or union? */
    if (type->kind == CS_TYPE_ARRAY)
    {
        return true;
    }

    if (has_basic_type(type, CS_STRUCT_TYPE) || has_basic_type(type, CS_UNION_TYPE))
    {
        return true;
    }

    return false;
}

bool cs_type_is_scalar(TypeSpecifier *type)
{
    if (!type)
    {
        return false;
    }

    if (cs_type_is_bool(type) || cs_type_is_integral(type) || cs_type_is_floating(type))
    {
        return true;
    }

    if (cs_type_is_pointer(type))
    {
        return true;
    }

    return false;
}

/* Helper to recursively build type string (walks root to deepest child, outputs child first) */
static void type_to_string_recursive(TypeSpecifier *type, char *buffer,
                                     int buffer_size, bool is_root)
{
    if (!type)
    {
        return;
    }

    /* Recurse to child first (deepest child gets output first) */
    if (type->child)
    {
        type_to_string_recursive(type->child, buffer, buffer_size, false);
    }

    /* Output this node's contribution */
    if (type->kind == CS_TYPE_POINTER)
    {
        append_token(buffer, buffer_size, "*");
    }
    else if (type->kind == CS_TYPE_ARRAY)
    {
        append_token(buffer, buffer_size, "[]");
    }
    else if (type->kind == CS_TYPE_BASIC || type->kind == CS_TYPE_NAMED)
    {
        /* Only output base type at the deepest child (no child) */
        if (!type->child)
        {
            const char *name = cs_type_user_type_name(type);
            if (type->kind == CS_TYPE_NAMED && name)
            {
                if (type->is_unsigned)
                {
                    append_token(buffer, buffer_size, "unsigned ");
                }
                append_token(buffer, buffer_size, name);
            }
            else
            {
                if (type->is_unsigned)
                {
                    append_token(buffer, buffer_size, "unsigned ");
                }
                append_token(buffer, buffer_size,
                             basic_type_name(cs_type_basic_type(type)));
                if (name)
                {
                    append_token(buffer, buffer_size, " ");
                    append_token(buffer, buffer_size, name);
                }
            }
        }
    }
}

void cs_type_to_string(TypeSpecifier *type, char *buffer, int buffer_size)
{
    if (!buffer || buffer_size == 0)
    {
        return;
    }

    buffer[0] = '\0';

    if (!type)
    {
        append_token(buffer, buffer_size, "<null>");
        return;
    }

    type_to_string_recursive(type, buffer, buffer_size, true);
}

/* ============================================================
 * Type Creation Functions
 * (Moved from create.c - only cminor_type.c should access raw type fields)
 * ============================================================ */

static TypeSpecifier *cs_allocate_type_specifier()
{
    TypeSpecifier *ts = (TypeSpecifier *)calloc(1, sizeof(TypeSpecifier));
    ts->kind = CS_TYPE_BASIC;
    ts->child = NULL;
    ts->is_typedef = false;
    ts->u.basic.basic_type = CS_VOID_TYPE;
    ts->u.basic.struct_members = NULL;
    return ts;
}

static TypeSpecifier *cs_set_child(TypeSpecifier *parent, TypeSpecifier *child)
{
    if (!parent)
        return child;
    parent->child = child;
    return parent;
}

TypeSpecifier *cs_create_type_specifier(CS_BasicType type)
{
    TypeSpecifier *ts = cs_allocate_type_specifier();
    ts->kind = CS_TYPE_BASIC;
    ts->u.basic.basic_type = type;
    ts->u.basic.struct_members = NULL;
    return ts;
}

TypeSpecifier *cs_create_named_type_specifier(CS_BasicType type,
                                              char *user_type_name)
{
    TypeSpecifier *ts = cs_allocate_type_specifier();
    ts->kind = CS_TYPE_NAMED;
    ts->u.named.basic_type = type;
    ts->u.named.id.name = user_type_name;
    ts->u.named.struct_members = NULL;
    return ts;
}

TypeSpecifier *cs_copy_type_specifier(TypeSpecifier *type)
{
    if (!type)
        return NULL;
    TypeSpecifier *copy = cs_allocate_type_specifier();
    copy->kind = type->kind;
    copy->is_typedef = type->is_typedef;
    copy->is_unsigned = type->is_unsigned;
    switch (type->kind)
    {
    case CS_TYPE_BASIC:
        copy->u.basic.basic_type = type->u.basic.basic_type;
        copy->u.basic.struct_members = type->u.basic.struct_members;
        break;
    case CS_TYPE_NAMED:
        copy->u.named.basic_type = type->u.named.basic_type;
        copy->u.named.id = type->u.named.id; /* Copy entire TypeIdentity */
        copy->u.named.struct_members = type->u.named.struct_members;
        break;
    case CS_TYPE_ARRAY:
        copy->u.array.array_size = type->u.array.array_size;
        break;
    case CS_TYPE_POINTER:
        break;
    }
    if (type->child)
    {
        TypeSpecifier *child_copy = cs_copy_type_specifier(type->child);
        cs_set_child(copy, child_copy);
    }
    return copy;
}

TypeSpecifier *cs_wrap_pointer(TypeSpecifier *base, int pointer_level)
{
    TypeSpecifier *current = base;
    while (pointer_level-- > 0)
    {
        TypeSpecifier *wrapper = cs_allocate_type_specifier();
        wrapper->kind = CS_TYPE_POINTER;
        cs_set_child(wrapper, current);
        current = wrapper;
    }
    return current;
}

TypeSpecifier *cs_wrap_array(TypeSpecifier *base, Expression *array_size)
{
    TypeSpecifier *wrapper = cs_allocate_type_specifier();
    wrapper->kind = CS_TYPE_ARRAY;
    wrapper->u.array.array_size = array_size;
    cs_set_child(wrapper, base);
    return wrapper;
}

/* ============================================================
 * typedef Flag Access
 * ============================================================ */

bool cs_type_is_typedef(TypeSpecifier *type)
{
    if (!type)
    {
        return false;
    }
    return type->is_typedef;
}

void cs_type_set_typedef(TypeSpecifier *type, bool is_typedef)
{
    if (!type)
    {
        return;
    }
    type->is_typedef = is_typedef;
}

/* ============================================================
 * Unsigned Flag Access
 * ============================================================ */

bool cs_type_is_unsigned(TypeSpecifier *type)
{
    if (!type)
    {
        return false;
    }
    return type->is_unsigned;
}

void cs_type_set_unsigned(TypeSpecifier *type, bool is_unsigned)
{
    if (!type)
    {
        return;
    }
    type->is_unsigned = is_unsigned;
}

bool cs_type_is_const(TypeSpecifier *type)
{
    if (!type)
    {
        return false;
    }
    return type->is_const;
}

void cs_type_set_const(TypeSpecifier *type, bool is_const)
{
    if (!type)
    {
        return;
    }
    type->is_const = is_const;
}

/* Note: ParsedType functions have been moved to parsed_type.c */

bool cs_type_signedness_matches(TypeSpecifier *left, TypeSpecifier *right)
{
    if (!left || !right)
    {
        return true; /* Allow null types to pass */
    }
    /* Only check signedness for integral types */
    if (!cs_type_is_integral(left) || !cs_type_is_integral(right))
    {
        return true; /* Non-integral types always match */
    }
    return cs_type_is_unsigned(left) == cs_type_is_unsigned(right);
}

/* Check if type is smaller than int (char, short) */
bool cs_type_is_small_int(TypeSpecifier *type)
{
    return cs_type_is_char_exact(type) || cs_type_is_short_exact(type);
}

/* ── Operation-specific mixing rules ──
 *
 * New integer system design:
 * - Small int types (char/short) promote based on signedness:
 *   - signed char/short -> int (sign extension)
 *   - unsigned char/short -> uint (zero extension)
 * - int promotes to long/ulong based on signedness when mixed with long
 * - Mixed signedness is allowed for arithmetic (two's complement same bit pattern)
 * - Mixed signedness is prohibited for comparison (different semantics)
 * - Division requires matching signedness (different JVM instructions)
 */

/* Arithmetic operations (+, -, *, bitwise): always allow mixed signedness.
 * Two's complement representation means the bit operations are identical. */
bool cs_type_can_mix_for_arithmetic(TypeSpecifier *left, TypeSpecifier *right)
{
    if (!left || !right)
    {
        return true;
    }
    /* Only check for integral types - float/double always mixable */
    if (!cs_type_is_integral(left) || !cs_type_is_integral(right))
    {
        return true;
    }
    /* Arithmetic allows all integral type combinations */
    return true;
}

/* Check if two integral types can be safely mixed (signedness check).
 * Allows mixing when:
 *   - Same signedness
 *   - Small unsigned can safely widen to larger signed
 *     (e.g., unsigned char/short vs int/long)
 * Disallows:
 *   - unsigned int vs int (large values don't fit) */
bool cs_type_can_mix_sign(TypeSpecifier *left, TypeSpecifier *right)
{
    if (!left || !right)
    {
        return true;
    }
    /* Only check for integral types */
    if (!cs_type_is_integral(left) || !cs_type_is_integral(right))
    {
        return true;
    }

    bool left_unsigned = cs_type_is_unsigned(left);
    bool right_unsigned = cs_type_is_unsigned(right);

    /* Same signedness is always OK */
    if (left_unsigned == right_unsigned)
    {
        return true;
    }

    /* Check if small unsigned can safely widen to larger signed */
    if (cs_type_can_widen_cross_sign(left, right) ||
        cs_type_can_widen_cross_sign(right, left))
    {
        return true;
    }

    /* Otherwise, signedness mismatch is not allowed */
    return false;
}

/* Comparison operations: use common sign mixing rule */
bool cs_type_can_mix_for_comparison(TypeSpecifier *left, TypeSpecifier *right)
{
    return cs_type_can_mix_sign(left, right);
}

/* Division/modulo operations: use common sign mixing rule */
bool cs_type_can_mix_for_division(TypeSpecifier *left, TypeSpecifier *right)
{
    return cs_type_can_mix_sign(left, right);
}

/* Check if unsigned source can be widened to signed target (cross-sign widening).
 * Only allows widening where all source values fit in target:
 *   uchar (0-255) -> short/int/long: OK
 *   ushort (0-65535) -> int/long: OK
 *   uint (0-2^32-1) -> long: OK
 * Does NOT allow signed -> unsigned widening (negative values lost) */
bool cs_type_can_widen_cross_sign(TypeSpecifier *source, TypeSpecifier *target)
{
    if (!source || !target)
    {
        return false;
    }
    if (!cs_type_is_integral(source) || !cs_type_is_integral(target))
    {
        return false;
    }

    /* Same signedness - not cross-sign, use regular widening */
    if (cs_type_is_unsigned(source) == cs_type_is_unsigned(target))
    {
        return false;
    }

    /* Signed -> unsigned: not allowed (negative values lost) */
    if (!cs_type_is_unsigned(source) && cs_type_is_unsigned(target))
    {
        return false;
    }

    /* Unsigned -> signed: check if all source values fit in target */
    /* uchar (8bit) -> short (16bit signed) / int (32bit signed) / long (64bit signed): OK */
    if (cs_type_is_char_exact(source))
    {
        return cs_type_is_short_exact(target) ||
               cs_type_is_int_exact(target) ||
               cs_type_is_long_exact(target);
    }
    /* ushort (16bit) -> int (32bit signed) / long (64bit signed): OK */
    if (cs_type_is_short_exact(source))
    {
        return cs_type_is_int_exact(target) || cs_type_is_long_exact(target);
    }
    /* uint (32bit) -> long (64bit signed): OK */
    if (cs_type_is_int_exact(source))
    {
        return cs_type_is_long_exact(target);
    }

    return false;
}

/* ============================================================
 * Basic Type Setter
 * ============================================================ */

void cs_type_set_basic_type(TypeSpecifier *type, CS_BasicType basic)
{
    if (!type)
    {
        return;
    }
    switch (type->kind)
    {
    case CS_TYPE_BASIC:
        type->u.basic.basic_type = basic;
        break;
    case CS_TYPE_NAMED:
        type->u.named.basic_type = basic;
        break;
    default:
        break;
    }
}

/* ============================================================
 * Type Canonicalization and Compatibility
 * ============================================================ */

/* Get canonical (resolved) type for a type.
 * Resolves typedef aliases using HeaderStore. */
TypeSpecifier *cs_type_canonical(TypeSpecifier *type, HeaderIndex *index)
{
    if (!type)
    {
        return NULL;
    }

    /* For pointer/array types, we compare canonical child types */
    if (type->kind == CS_TYPE_POINTER || type->kind == CS_TYPE_ARRAY)
    {
        return type;
    }

    /* For named types, try to resolve via typedef in HeaderIndex */
    if (type->kind == CS_TYPE_NAMED)
    {
        const char *name = cs_type_user_type_name(type);
        if (name && index)
        {
            TypedefDefinition *td = header_index_find_typedef(index, name);
            if (td && td->canonical)
            {
                return td->canonical;
            }
        }
    }

    return type;
}

/* Compare two types for structural equality, considering canonical forms */
static bool types_structurally_equal(TypeSpecifier *a, TypeSpecifier *b,
                                     HeaderIndex *index)
{
    if (!a || !b)
    {
        return a == b;
    }

    /* Get canonical forms */
    TypeSpecifier *ca = cs_type_canonical(a, index);
    TypeSpecifier *cb = cs_type_canonical(b, index);

    /* Compare kinds */
    if (ca->kind != cb->kind)
    {
        return false;
    }

    switch (ca->kind)
    {
    case CS_TYPE_BASIC:
        return cs_type_basic_type(ca) == cs_type_basic_type(cb) &&
               ca->is_unsigned == cb->is_unsigned;

    case CS_TYPE_NAMED:
        /* Compare basic types and type identity */
        if (cs_type_basic_type(ca) != cs_type_basic_type(cb))
        {
            return false;
        }
        return cs_type_named_id_equals(ca, cb);

    case CS_TYPE_POINTER:
    case CS_TYPE_ARRAY:
        return types_structurally_equal(ca->child, cb->child, index);

    default:
        return false;
    }
}

/* Check if two types are compatible for assignment */
bool cs_type_compatible(TypeSpecifier *target, TypeSpecifier *source,
                        HeaderIndex *index)
{
    if (!target || !source)
    {
        return false;
    }

    /* Get canonical forms */
    TypeSpecifier *ct = cs_type_canonical(target, index);
    TypeSpecifier *cs_src = cs_type_canonical(source, index);

    /* Direct structural equality */
    if (types_structurally_equal(ct, cs_src, index))
    {
        return true;
    }

    /* Pointer signedness compatibility (char* <-> unsigned char*) */
    if (cs_type_pointer_signedness_compatible(ct, cs_src, index))
    {
        return true;
    }

    /* Numeric type compatibility (handled elsewhere for conversions) */

    return false;
}

/* Helper: Check if two child types differ only in signedness */
static bool child_types_signedness_only_diff(TypeSpecifier *a, TypeSpecifier *b,
                                             HeaderIndex *index)
{
    if (!a || !b)
    {
        return false;
    }

    /* Get canonical forms of children */
    TypeSpecifier *ca = cs_type_canonical(a, index);
    TypeSpecifier *cb = cs_type_canonical(b, index);

    /* Both must be the same kind */
    if (ca->kind != cb->kind)
    {
        return false;
    }

    /* For basic types, check if only signedness differs */
    if (ca->kind == CS_TYPE_BASIC)
    {
        /* Must be the same basic type */
        if (cs_type_basic_type(ca) != cs_type_basic_type(cb))
        {
            return false;
        }
        /* Must be an integral type (signedness only applies to integral types) */
        if (!cs_type_is_integral(ca))
        {
            return false;
        }
        /* Signedness may differ - that's what we're checking for */
        return true;
    }

    /* For pointer types, recursively check children */
    if (ca->kind == CS_TYPE_POINTER)
    {
        return child_types_signedness_only_diff(ca->child, cb->child, index);
    }

    /* For array types, recursively check children */
    if (ca->kind == CS_TYPE_ARRAY)
    {
        return child_types_signedness_only_diff(ca->child, cb->child, index);
    }

    return false;
}

/* Check if two pointer types differ only in signedness of pointed-to type */
bool cs_type_pointer_signedness_compatible(TypeSpecifier *target,
                                           TypeSpecifier *source,
                                           HeaderIndex *index)
{
    if (!target || !source)
    {
        return false;
    }

    /* Both must be pointers */
    if (!cs_type_is_pointer(target) || !cs_type_is_pointer(source))
    {
        return false;
    }

    /* Check if children differ only in signedness */
    return child_types_signedness_only_diff(target->child, source->child, index);
}

/* Check if switch and case types are compatible.
 * For integral types, allows implicit conversion if both are integral.
 * Value range checking is done separately in semantic analysis.
 *
 * Enum values are treated as integer constants, so enum cases are compatible
 * with integral switch types (and vice versa) if values fit in range. */
bool cs_type_switch_compatible(TypeSpecifier *switch_type, TypeSpecifier *case_type)
{
    if (!switch_type || !case_type)
    {
        return false;
    }

    /* Same type is always compatible */
    if (cs_type_equals(switch_type, case_type))
    {
        return true;
    }

    /* Both enums but different types: not compatible */
    if (cs_type_is_enum(switch_type) && cs_type_is_enum(case_type))
    {
        return false;
    }

    /* Enum vs integral: compatible (value range checked separately) */
    bool switch_is_int_like = cs_type_is_integral(switch_type) || cs_type_is_enum(switch_type);
    bool case_is_int_like = cs_type_is_integral(case_type) || cs_type_is_enum(case_type);

    if (switch_is_int_like && case_is_int_like)
    {
        return true;
    }

    return false;
}

/* Check if an integer value fits within the range of a given integral type.
 * Returns true if value fits within target_type's range. */
bool cs_type_value_fits_in(long value, bool value_is_unsigned, TypeSpecifier *target_type)
{
    if (!target_type || !cs_type_is_integral(target_type))
    {
        return false;
    }

    bool target_unsigned = cs_type_is_unsigned(target_type);
    CS_BasicType bt = cs_type_basic_type(target_type);

    /* Define ranges for each type */
    long min_val;
    long max_val;
    unsigned long umax_val;

    switch (bt)
    {
    case CS_CHAR_TYPE:
        if (target_unsigned)
        {
            min_val = 0;
            umax_val = 255;
        }
        else
        {
            min_val = -128;
            max_val = 127;
        }
        break;
    case CS_SHORT_TYPE:
        if (target_unsigned)
        {
            min_val = 0;
            umax_val = 65535;
        }
        else
        {
            min_val = -32768;
            max_val = 32767;
        }
        break;
    case CS_INT_TYPE:
        if (target_unsigned)
        {
            /* unsigned int: 0 to 4294967295 */
            if (value_is_unsigned)
            {
                return (unsigned long)value <= 4294967295UL;
            }
            return value >= 0;
        }
        else
        {
            min_val = -2147483648L;
            max_val = 2147483647L;
        }
        break;
    case CS_LONG_TYPE:
        /* long can hold any long value */
        return true;
    default:
        return false;
    }

    if (target_unsigned)
    {
        if (value_is_unsigned)
        {
            return (unsigned long)value <= umax_val;
        }
        return value >= 0 && (unsigned long)value <= umax_val;
    }
    else
    {
        if (value_is_unsigned)
        {
            return (unsigned long)value <= (unsigned long)max_val;
        }
        return value >= min_val && value <= max_val;
    }
}

/* ============================================================
 * Union Type Analysis
 * ============================================================ */

/* Check if type is a union */
bool cs_type_is_union(TypeSpecifier *type)
{
    if (!type)
    {
        return false;
    }
    CS_BasicType bt = cs_type_basic_type(type);
    return bt == CS_UNION_TYPE;
}

/* Check if type is a basic struct or union (anonymous or inline) */
bool cs_type_is_basic_struct_or_union(TypeSpecifier *type)
{
    if (!type)
    {
        return false;
    }
    CS_BasicType bt = cs_type_basic_type(type);
    return bt == CS_STRUCT_TYPE || bt == CS_UNION_TYPE;
}

/* Count struct/union members */
static int count_members(StructMember *members)
{
    int count = 0;
    for (StructMember *m = members; m; m = m->next)
    {
        count++;
    }
    return count;
}

static TypeSpecifier *resolve_member_type(StructMember *member)
{
    if (!member)
    {
        return NULL;
    }
    /* member->type should already be resolved during struct registration */
    return member->type;
}

/* Analyze union kind from member list directly */
CS_UnionKind cs_union_kind_from_members(StructMember *members)
{
    int member_count = count_members(members);

    if (member_count == 0)
    {
        return CS_UNION_KIND_UNSUPPORTED;
    }

    /* Check for int+float pattern (exactly 2 members) */
    if (member_count == 2)
    {
        StructMember *m1 = members;
        StructMember *m2 = members->next;
        TypeSpecifier *t1 = resolve_member_type(m1);
        TypeSpecifier *t2 = resolve_member_type(m2);

        /* int + float (either order) */
        bool is_int_float = (cs_type_is_int_exact(t1) && cs_type_is_float_exact(t2)) ||
                            (cs_type_is_float_exact(t1) && cs_type_is_int_exact(t2));
        if (is_int_float)
        {
            return CS_UNION_KIND_TYPE_PUNNING_INT_FLOAT;
        }

        /* long + double (either order) */
        bool is_long_double = (cs_type_is_long_exact(t1) && cs_type_is_double_exact(t2)) ||
                              (cs_type_is_double_exact(t1) && cs_type_is_long_exact(t2));
        if (is_long_double)
        {
            return CS_UNION_KIND_TYPE_PUNNING_LONG_DOUBLE;
        }
    }

    /* Check if all members are reference types (pointers or aggregates) */
    bool all_reference = true;
    bool has_primitive = false;
    for (StructMember *m = members; m; m = m->next)
    {
        TypeSpecifier *mt = resolve_member_type(m);
        if (cs_type_is_pointer(mt) || cs_type_is_aggregate(mt))
        {
            /* Reference type - OK */
        }
        else if (cs_type_is_primitive(mt))
        {
            has_primitive = true;
            all_reference = false;
        }
        else
        {
            all_reference = false;
        }
    }

    if (all_reference)
    {
        /* All reference types (pointers, aggregates) -> REFERENCE */
        /* JVM: single Object field + checkcast */
        return CS_UNION_KIND_REFERENCE;
    }

    if (has_primitive)
    {
        /* Mixed primitives not matching int/float or long/double */
        /* Use REFERENCE with box/unbox for other primitive combinations */
        return CS_UNION_KIND_REFERENCE;
    }

    /* Should not reach here - empty union? */
    return CS_UNION_KIND_REFERENCE;
}

/* Analyze union type and return its kind */
CS_UnionKind cs_union_kind(TypeSpecifier *type)
{
    if (!type || !cs_type_is_union(type))
    {
        return CS_UNION_KIND_NOT_UNION;
    }

    StructMember *members = cs_type_struct_members(type);
    return cs_union_kind_from_members(members);
}

/* Compute total size of array type (product of all dimensions).
 * Returns -1 for non-array types (basic types) or if any dimension is not a constant.
 * For pointers, returns 1. */
int cs_type_compute_array_size(TypeSpecifier *type)
{
    if (!type)
        return -1;

    if (cs_type_is_pointer(type))
        return 1;

    if (!cs_type_is_array(type))
        return -1; /* Basic type - invalid in Cminor sizeof */

    /* Compute product of all array dimensions */
    int total = 1;
    TypeSpecifier *current = type;
    while (current && cs_type_is_array(current))
    {
        Expression *size_expr = cs_type_array_size(current);
        if (!size_expr)
            return -1; /* No array size */

        int dim_size;
        if (size_expr->kind == INT_EXPRESSION)
        {
            dim_size = size_expr->u.int_value;
        }
        else if (size_expr->kind == IDENTIFIER_EXPRESSION &&
                 size_expr->u.identifier.is_enum_member)
        {
            /* Enum constant as array size */
            EnumMember *member = size_expr->u.identifier.u.enum_member;
            if (!member)
                return -1;
            dim_size = member->value;
        }
        else
        {
            return -1; /* Non-constant array size */
        }
        total *= dim_size;
        current = cs_type_child(current);
    }

    /* After traversing arrays, check what the element type is */
    if (current && cs_type_is_pointer(current))
    {
        /* Element type is pointer - multiply by pointer size (1) */
        return total;
    }

    /* Element type is basic type - return total (size of basic type is 1 for sizeof) */
    return total;
}
