#pragma once

/*
 * ParsedType - Lightweight type representation for parsing phase
 *
 * This module provides:
 * 1. ParsedType struct definition
 * 2. Functions for creating/manipulating ParsedType (used by parser)
 * 3. cs_resolve_type() for converting ParsedType to TypeSpecifier
 *    during semantic analysis
 *
 * ParsedType captures ONLY syntactic information from parsing.
 * Conversion to TypeSpecifier with proper type resolution happens
 * during semantic analysis via cs_resolve_type().
 */

#include "cminor_base.h"

/* ParsedType - Lightweight syntactic type representation for parsing phase.
 * Contains only what was parsed; does NOT contain full TypeIdentity.
 * Use cs_resolve_type() during semantic analysis to convert to TypeSpecifier
 * with proper type resolution. */
struct ParsedType_tag
{
    CS_TypeKind kind;
    CS_BasicType basic_type;
    CS_TypeNamespace name_space;
    char *name; /* Type name (e.g., "Color" or "Foo$0" for anonymous) */
    struct ParsedType_tag *child;
    Expression *array_size;
    bool is_unsigned;
    bool is_const;
};

/* ============================================================
 * ParsedType Creation (used by parser)
 * ============================================================ */

/* Create a basic type (int, char, void, etc.) */
ParsedType *cs_parsed_type_basic(CS_BasicType basic_type);

/* Create a named type (struct Foo, enum Bar, typedef name) */
ParsedType *cs_parsed_type_named(CS_BasicType basic_type, char *name);

/* Wrap with pointer level(s) */
ParsedType *cs_wrap_parsed_pointer(ParsedType *base, int pointer_level);

/* Wrap with array */
ParsedType *cs_wrap_parsed_array(ParsedType *base, Expression *array_size);

/* Copy a parsed type */
ParsedType *cs_copy_parsed_type(const ParsedType *type);

/* Set qualifiers */
void cs_parsed_type_set_unsigned(ParsedType *type, bool is_unsigned);
void cs_parsed_type_set_const(ParsedType *type, bool is_const);

/* ============================================================
 * Type Resolution (used during semantic analysis)
 * ============================================================ */

/*
 * Resolve a ParsedType to a TypeSpecifier.
 *
 * This is the main conversion function that:
 * 1. Converts syntactic type to semantic type
 * 2. Resolves typedef names using DeclarationRegistry
 * 3. Assigns TypeIdentity for anonymous struct/union/enum
 * 4. Validates type existence
 *
 * Must be called during semantic analysis when HeaderStore
 * is available and populated.
 *
 * Returns NULL on error (unknown type, etc.)
 */
TypeSpecifier *cs_resolve_type(const ParsedType *parsed,
                               HeaderStore *store,
                               CS_Compiler *compiler);

/* Forward declaration for HeaderIndex */
typedef struct HeaderIndex_tag HeaderIndex;

/* Resolve a ParsedType using HeaderIndex only (for header parsing).
 * This is a convenience wrapper that doesn't require a compiler context. */
TypeSpecifier *cs_resolve_type_with_index(const ParsedType *parsed,
                                          HeaderIndex *index);

/* ============================================================
 * Utility Functions
 * ============================================================ */

/* Create a ParsedType from a TypeSpecifier (for reverse conversion) */
ParsedType *cs_create_parsed_type_from_specifier(TypeSpecifier *type);
