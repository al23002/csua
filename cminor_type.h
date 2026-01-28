#pragma once

#include <stddef.h>

#include "cminor_base.h"
#include "type_specifier.h"
#include "definitions.h"

enum
{
    CS_TYPE_STRING_MAX = 128
};

typedef enum
{
    CS_TYPE_CATEGORY_INVALID = 0,
    CS_TYPE_CATEGORY_BASIC,
    CS_TYPE_CATEGORY_POINTER,
    CS_TYPE_CATEGORY_ARRAY,
    CS_TYPE_CATEGORY_NAMED
} CS_TypeCategory;

typedef struct
{
    TypeSpecifier *type;
    CS_TypeCategory category;
} CS_TypeInfo;

CS_TypeInfo cs_type_info(TypeSpecifier *type);

/* ── Explicit Traversal API ──
 * Use these when you need to explicitly traverse the type tree.
 * The caller is responsible for traversal decisions. */
TypeSpecifier *cs_type_child(TypeSpecifier *type);
TypeSpecifier *cs_type_box_reference(TypeSpecifier *value_type);
TypeSpecifier *cs_type_reference_for_address(TypeSpecifier *value_type,
                                             bool *requires_heap_lift);

/* ── Scalar Type Queries ──
 * These check if the type IS the scalar type directly.
 * Returns FALSE for pointers/arrays, even if they point to the type.
 * Examples:
 *   cs_type_is_int_exact(int)    → true
 *   cs_type_is_int_exact(int*)   → false
 *   cs_type_is_int_exact(int[])  → false
 */

/* Get the basic type of a scalar type (CS_TYPE_BASIC or CS_TYPE_NAMED).
 * Returns CS_BASIC_TYPE_PLUS_ONE for non-scalar types (arrays, pointers). */
CS_BasicType cs_type_basic_type(TypeSpecifier *type);

/* Exact type queries (for JVM type selection) */
bool cs_type_is_char_exact(TypeSpecifier *type);   /* char/byte only */
bool cs_type_is_short_exact(TypeSpecifier *type);  /* short only */
bool cs_type_is_int_exact(TypeSpecifier *type);    /* int only (includes anonymous enum) */
bool cs_type_is_long_exact(TypeSpecifier *type);   /* long only */
bool cs_type_is_float_exact(TypeSpecifier *type);  /* float only */
bool cs_type_is_double_exact(TypeSpecifier *type); /* double only */
bool cs_type_is_enum(TypeSpecifier *type);         /* any enum type */
bool cs_type_is_named_enum(TypeSpecifier *type);   /* named enum only (Java enum) */

/* Other scalar type queries */
bool cs_type_is_bool(TypeSpecifier *type);
bool cs_type_is_void(TypeSpecifier *type);
bool cs_type_is_void_pointer(TypeSpecifier *type);

/* Numeric type category queries */
bool cs_type_is_integral(TypeSpecifier *type); /* char/short/int/long */
bool cs_type_is_floating(TypeSpecifier *type); /* float/double */
bool cs_type_is_numeric(TypeSpecifier *type);  /* all numeric types */

/* ── Integer Promotion (Two-Step Model) ──
 *
 * Step 1: Unary Promotion (cs_type_unary_promoted)
 *   Each operand is promoted INDEPENDENTLY:
 *   - signed char/short   -> int  (sign extension)
 *   - unsigned char/short -> uint (zero extension)
 *   - int/uint/long/ulong -> unchanged
 *
 * Step 2: Binary Promotion (cs_type_binary_promoted_specifier)
 *   Combine two promoted types:
 *   - float/double: standard floating point rules
 *   - int/uint/long/ulong: larger type wins, unsigned wins if same size
 */

/* Unary integer promotion: small_int -> int/uint based on signedness */
TypeSpecifier *cs_type_unary_promoted(TypeSpecifier *type);

/* Binary numeric promotion: combine two types after unary promotion */
TypeSpecifier *cs_type_binary_promoted_specifier(TypeSpecifier *left,
                                                 TypeSpecifier *right);

/* Check if widening from source to target type is needed for JVM.
 * JVM stores char/short/int all as int, so only checks for long/float/double. */
bool cs_type_needs_widening_to(TypeSpecifier *source, TypeSpecifier *target);

/* Get widening cast type from source to target type.
 * Returns 0 if no cast needed or not possible. */
CS_CastType cs_type_widening_cast_to(TypeSpecifier *source, TypeSpecifier *target);

/* Check if this type is a primitive scalar (void, char, short, int, long, float, double, bool).
 * Does NOT walk to child - checks this type node directly. */
bool cs_type_is_primitive(TypeSpecifier *type);

bool cs_type_is_pointer(TypeSpecifier *type);
bool cs_type_is_array(TypeSpecifier *type);
bool cs_type_is_named(TypeSpecifier *type);
bool cs_type_is_basic(TypeSpecifier *type);

/* Get the kind of this type (for switch statements) */
CS_TypeKind cs_type_kind(TypeSpecifier *type);

/* Compare basic types of two scalar types (with JVM-style normalization).
 * Does NOT walk to child - compares types directly. */
bool cs_type_same_basic(TypeSpecifier *lhs, TypeSpecifier *rhs);
bool cs_type_equals(TypeSpecifier *lhs, TypeSpecifier *rhs);
bool cs_type_is_aggregate(TypeSpecifier *type);
bool cs_type_is_scalar(TypeSpecifier *type);
/* TypeIdentity comparison for named types */
bool cs_type_named_id_equals(TypeSpecifier *a, TypeSpecifier *b);
const char *cs_type_user_type_name(TypeSpecifier *type);
void cs_type_set_user_type_name(TypeSpecifier *type, const char *name);
bool cs_type_identity_is_anonymous(TypeIdentity *id);
StructMember *cs_type_struct_members(TypeSpecifier *type);
void cs_type_set_struct_members(TypeSpecifier *type, StructMember *members);
Expression *cs_type_array_size(TypeSpecifier *type);
void cs_type_set_array_size(TypeSpecifier *type, Expression *array_size);
void cs_type_to_string(TypeSpecifier *type, char *buffer, int buffer_size);

/* typedef flag access */
bool cs_type_is_typedef(TypeSpecifier *type);
void cs_type_set_typedef(TypeSpecifier *type, bool is_typedef);

/* unsigned flag access */
bool cs_type_is_unsigned(TypeSpecifier *type);
void cs_type_set_unsigned(TypeSpecifier *type, bool is_unsigned);

/* const flag access */
bool cs_type_is_const(TypeSpecifier *type);
void cs_type_set_const(TypeSpecifier *type, bool is_const);

/* Note: ParsedType functions have been moved to parsed_type.h/c */

/* Check if two numeric types have matching signedness (both signed or both unsigned).
 * Returns false if signedness differs - this is an error in Cminor. */
bool cs_type_signedness_matches(TypeSpecifier *left, TypeSpecifier *right);

/* Check if type is smaller than int (char, short) - promotes to int in binary ops */
bool cs_type_is_small_int(TypeSpecifier *type);

/* ── Operation-specific mixing rules ──
 * Two's complement representation means arithmetic and bitwise operations
 * produce the same bit pattern regardless of signedness.
 * Only comparison and division differ based on signedness. */

/* Arithmetic operations (+, -, *, bitwise): always allow mixed signedness.
 * The result type is the promoted type (unsigned if either operand is unsigned). */
bool cs_type_can_mix_for_arithmetic(TypeSpecifier *left, TypeSpecifier *right);

/* Comparison operations (<, >, <=, >=, ==, !=): prohibit mixed signedness.
 * Comparison semantics differ between signed and unsigned values. */
bool cs_type_can_mix_for_comparison(TypeSpecifier *left, TypeSpecifier *right);

/* Division/modulo operations: require matching signedness.
 * JVM uses different instructions for signed (idiv/ldiv) vs unsigned
 * (Integer.divideUnsigned/Long.divideUnsigned). */
bool cs_type_can_mix_for_division(TypeSpecifier *left, TypeSpecifier *right);

/* Check if unsigned source can be widened to signed target (cross-sign widening).
 * e.g., ushort -> int (OK), uint -> long (OK), short -> uint (NG) */
bool cs_type_can_widen_cross_sign(TypeSpecifier *source, TypeSpecifier *target);

/* Set basic type for a type specifier */
void cs_type_set_basic_type(TypeSpecifier *type, CS_BasicType basic);

/* Forward declaration for HeaderIndex */
typedef struct HeaderIndex_tag HeaderIndex;

/* Get canonical (resolved) type for a type.
 * Resolves typedef aliases (e.g., int32_t -> int, Foo -> struct Foo_tag).
 * Returns the type itself if no typedef resolution needed. */
TypeSpecifier *cs_type_canonical(TypeSpecifier *type, HeaderIndex *index);

/* Check if two types are compatible for assignment.
 * Uses canonical types for comparison, so int32_t and int are compatible.
 * Also handles struct Foo_tag vs Foo typedef equivalence. */
bool cs_type_compatible(TypeSpecifier *target, TypeSpecifier *source,
                        HeaderIndex *index);

/* Check if two pointer types differ only in signedness of pointed-to type.
 * e.g., char* and unsigned char* are signedness-compatible.
 * This is allowed because JVM has no unsigned types (both become byte[]). */
bool cs_type_pointer_signedness_compatible(TypeSpecifier *target,
                                           TypeSpecifier *source,
                                           HeaderIndex *index);

/* Check if enum type is compatible with int for switch/case.
 * For integral types, allows implicit narrowing if value fits in switch type. */
bool cs_type_switch_compatible(TypeSpecifier *switch_type, TypeSpecifier *case_type);

/* Check if an integer value fits within the range of a given integral type.
 * value: the constant value to check
 * value_is_unsigned: true if value should be treated as unsigned
 * target_type: the type to check against (char, short, int, long, or unsigned variants)
 * Returns true if value fits within target_type's range. */
bool cs_type_value_fits_in(long value, bool value_is_unsigned, TypeSpecifier *target_type);

/* ── Union Type Analysis ──
 * Cminor supports limited union patterns for JVM compatibility:
 *
 * 1. TYPE_PUNNING_INT_FLOAT: union { int i; float f; }
 *    - JVM: single int field + Float.floatToRawIntBits/intBitsToFloat
 *
 * 2. TYPE_PUNNING_LONG_DOUBLE: union { long l; double d; }
 *    - JVM: single long field + Double.doubleToRawLongBits/longBitsToDouble
 *
 * 3. REFERENCE: all reference types (pointers, aggregates, boxed primitives)
 *    - JVM: single Object field (_ref) + checkcast/box/unbox
 *    - Pointers: checkcast to target pointer type
 *    - Primitives: box/unbox (Integer, Long, Float, Double)
 *    - Struct/union values: checkcast to target class
 *
 * 4. UNSUPPORTED: unsupported patterns (currently none)
 */
typedef enum
{
    CS_UNION_KIND_NOT_UNION = 0,
    CS_UNION_KIND_TYPE_PUNNING_INT_FLOAT,
    CS_UNION_KIND_TYPE_PUNNING_LONG_DOUBLE,
    CS_UNION_KIND_REFERENCE,
    CS_UNION_KIND_UNSUPPORTED
} CS_UnionKind;

/* Analyze union type and return its kind.
 * Returns CS_UNION_KIND_NOT_UNION for non-union types. */
CS_UnionKind cs_union_kind(TypeSpecifier *type);

/* Analyze union kind from member list directly.
 * Used when only StructDefinition is available (no TypeSpecifier). */
CS_UnionKind cs_union_kind_from_members(StructMember *members);

/* Check if type is a union */
bool cs_type_is_union(TypeSpecifier *type);

/* Check if type is a basic struct or union (anonymous or inline) */
bool cs_type_is_basic_struct_or_union(TypeSpecifier *type);

/* Compute total size of array type (product of all dimensions).
 * Returns -1 for non-array types (basic types) or if any dimension is not a constant.
 * For pointers, returns 1. */
int cs_type_compute_array_size(TypeSpecifier *type);

/* ── Type Creation Helpers ── */
TypeSpecifier *cs_create_type_specifier(CS_BasicType type);
TypeSpecifier *cs_create_named_type_specifier(CS_BasicType type,
                                              char *user_type_name);
TypeSpecifier *cs_copy_type_specifier(TypeSpecifier *type);
TypeSpecifier *cs_wrap_pointer(TypeSpecifier *base, int pointer_level);
TypeSpecifier *cs_wrap_array(TypeSpecifier *base, Expression *array_size);
