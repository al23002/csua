#pragma once

/*
 * cminor_base.h - Basic type definitions and forward declarations
 *
 * This header contains only:
 * 1. Basic enums (CS_BasicType, CS_TypeKind, etc.)
 * 2. Forward declarations of major types
 *
 * No struct definitions - those are in their respective headers.
 */

#include <stddef.h>
#include <stdint.h>

/* Length-prefixed string for string literals (supports embedded nulls) */
typedef struct
{
    uint8_t *data; /* UTF-8 bytes (not null-terminated) */
    int len;       /* byte length */
} CS_String;

/* Forward declarations - these define typedefs for structs defined elsewhere.
 * For Cminor codegen: struct definitions must be visible when typedef is used.
 * When self-compiling, ensure the header with struct definition is included. */
typedef struct Expression_tag Expression;
typedef struct ExpressionList_tag ExpressionList;
typedef struct CompilerContext_tag CompilerContext;
typedef struct TranslationUnit_tag TranslationUnit;
typedef struct TranslationUnit_tag CS_Compiler; /* Compatibility alias */
typedef struct CS_Creator_tag CS_Creator;
typedef struct Scanner_tag Scanner;
typedef struct StructMember_tag StructMember;
typedef struct StructDefinition_tag StructDefinition;
typedef struct EnumMember_tag EnumMember;
typedef struct EnumDefinition_tag EnumDefinition;
typedef struct TypedefDefinition_tag TypedefDefinition;
typedef struct TypeSpecifier_tag TypeSpecifier;
typedef struct ParsedType_tag ParsedType;
typedef struct TypeIdentity_tag TypeIdentity;
typedef struct Statement_tag Statement;
typedef struct StatementList_tag StatementList;
typedef struct DeclarationList_tag DeclarationList;
typedef struct FunctionDeclarationList_tag FunctionDeclarationList;
typedef struct HeaderStore_tag HeaderStore;

/* Basic type enum */
typedef enum
{
    CS_VOID_TYPE,
    CS_CHAR_TYPE,
    CS_SHORT_TYPE,
    CS_BOOLEAN_TYPE,
    CS_INT_TYPE,
    CS_LONG_TYPE,
    CS_FLOAT_TYPE,
    CS_DOUBLE_TYPE,
    CS_STRUCT_TYPE,
    CS_UNION_TYPE,
    CS_ENUM_TYPE,
    CS_TYPEDEF_NAME,
    CS_BASIC_TYPE_PLUS_ONE,
} CS_BasicType;

/* Type kind enum */
typedef enum
{
    CS_TYPE_BASIC,
    CS_TYPE_POINTER,
    CS_TYPE_ARRAY,
    CS_TYPE_NAMED
} CS_TypeKind;

/* Type namespace enum */
typedef enum
{
    CS_TYPE_NAMESPACE_NONE,
    CS_TYPE_NAMESPACE_STRUCT,
    CS_TYPE_NAMESPACE_UNION,
    CS_TYPE_NAMESPACE_ENUM,
    CS_TYPE_NAMESPACE_TYPEDEF
} CS_TypeNamespace;

/* Implicit type conversion operators (inserted during semantic analysis)
 * These follow Java's numeric type promotion rules:
 * - byte, short, char are promoted to int for arithmetic operations
 * - If either operand is double, the other is converted to double
 * - Otherwise, if either operand is float, the other is converted to float
 * - Otherwise, if either operand is long, the other is converted to long
 * - Otherwise, both operands are converted to int
 */
typedef enum
{
    /* Widening conversions (for arithmetic promotion) */
    CS_CHAR_TO_INT = 1, /* JVM: automatic (byte is stored as int on stack) */
    CS_SHORT_TO_INT,    /* JVM: automatic (short is stored as int on stack) */
    CS_INT_TO_LONG,     /* JVM: i2l (signed: sign-extend) */
    CS_INT_TO_FLOAT,    /* JVM: i2f */
    CS_INT_TO_DOUBLE,   /* JVM: i2d */
    CS_LONG_TO_FLOAT,   /* JVM: l2f */
    CS_LONG_TO_DOUBLE,  /* JVM: l2d */
    CS_FLOAT_TO_DOUBLE, /* JVM: f2d */

    /* Unsigned widening conversions (zero-extend instead of sign-extend) */
    CS_UCHAR_TO_INT,  /* JVM: iand 0xFF (zero-extend byte to int) */
    CS_USHORT_TO_INT, /* JVM: iand 0xFFFF (zero-extend short to int) */
    CS_UINT_TO_ULONG, /* JVM: i2l + mask with 0xFFFFFFFFL */

    /* Narrowing conversions (for assignments) */
    CS_INT_TO_CHAR,     /* JVM: i2b (byte in C is char) */
    CS_INT_TO_SHORT,    /* JVM: i2s */
    CS_LONG_TO_INT,     /* JVM: l2i */
    CS_FLOAT_TO_INT,    /* JVM: f2i */
    CS_FLOAT_TO_LONG,   /* JVM: f2l */
    CS_DOUBLE_TO_INT,   /* JVM: d2i */
    CS_DOUBLE_TO_LONG,  /* JVM: d2l */
    CS_DOUBLE_TO_FLOAT, /* JVM: d2f */
} CS_CastType;
