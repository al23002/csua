#pragma once

/*
 * type_specifier.h - Semantic type representation
 *
 * TypeSpecifier is the semantic type used after parsing.
 * It contains fully resolved type information including:
 * - Type identity (for struct/union/enum)
 * - Struct members (inline for convenience)
 * - Array sizes
 *
 * For the syntactic type used during parsing, see parsed_type.h
 */

#include "cminor_base.h"

/* Type identity - unified identification for named and anonymous types.
 * All types have a name. Anonymous types get generated names like "Foo$0".
 * Named types get qualified names like "preprocessor_h$Preprocessor".
 *
 * search_name: Original C name for lookup (e.g., "Preprocessor").
 *              NULL for anonymous types.
 * name:        Class name for codegen (e.g., "preprocessor_h$Preprocessor").
 *
 * One-way lookup: search by search_name, get name (class name).
 */
struct TypeIdentity_tag
{
    char *name;        /* Class name: "preprocessor_h$Preprocessor" or "foo$0" */
    char *search_name; /* Search key: "Preprocessor" (NULL for anonymous) */
};

struct TypeSpecifier_tag
{
    CS_TypeKind kind;
    struct TypeSpecifier_tag *child;
    bool is_typedef;
    bool is_unsigned; /* true for unsigned char/short/int/long */
    bool is_const;    /* true for const-qualified types */
    union
    {
        struct
        {
            CS_BasicType basic_type;
            StructMember *struct_members;
        } basic;
        struct
        {
            CS_BasicType basic_type;
            TypeIdentity id; /* Type identity (name or anonymous index) */
            StructMember *struct_members;
        } named;
        struct
        {
            Expression *array_size;
        } array;
    } u;
};
