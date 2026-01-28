/*
 * ParsedType - Parsing phase type representation
 *
 * This module handles:
 * 1. ParsedType creation and manipulation (used by parser)
 * 2. Type resolution: ParsedType -> TypeSpecifier (used by semantic analysis)
 */

#include "parsed_type.h"
#include "create.h"
#include "cminor_type.h"
#include "header_store.h"
#include "header_index.h"
#include "definitions.h"
#include "compiler.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============================================================
 * ParsedType Allocation
 * ============================================================ */

static ParsedType *cs_allocate_parsed_type()
{
    return (ParsedType *)calloc(1, sizeof(ParsedType));
}

/* ============================================================
 * ParsedType Creation (used by parser)
 * ============================================================ */

ParsedType *cs_parsed_type_basic(CS_BasicType basic_type)
{
    ParsedType *parsed = cs_allocate_parsed_type();
    parsed->kind = CS_TYPE_BASIC;
    parsed->basic_type = basic_type;
    parsed->name_space = CS_TYPE_NAMESPACE_NONE;
    parsed->name = NULL;
    parsed->child = NULL;
    parsed->array_size = NULL;
    parsed->is_unsigned = false;
    parsed->is_const = false;
    return parsed;
}

ParsedType *cs_parsed_type_named(CS_BasicType basic_type, char *name)
{
    ParsedType *parsed = cs_allocate_parsed_type();
    parsed->kind = CS_TYPE_NAMED;
    parsed->basic_type = basic_type;

    if (basic_type == CS_STRUCT_TYPE)
    {
        parsed->name_space = CS_TYPE_NAMESPACE_STRUCT;
    }
    else if (basic_type == CS_UNION_TYPE)
    {
        parsed->name_space = CS_TYPE_NAMESPACE_UNION;
    }
    else if (basic_type == CS_ENUM_TYPE)
    {
        parsed->name_space = CS_TYPE_NAMESPACE_ENUM;
    }
    else if (basic_type == CS_TYPEDEF_NAME)
    {
        parsed->name_space = CS_TYPE_NAMESPACE_TYPEDEF;
    }
    else
    {
        parsed->name_space = CS_TYPE_NAMESPACE_NONE;
    }

    parsed->name = name;
    parsed->child = NULL;
    parsed->array_size = NULL;
    parsed->is_unsigned = false;
    parsed->is_const = false;
    return parsed;
}

ParsedType *cs_wrap_parsed_pointer(ParsedType *base, int pointer_level)
{
    ParsedType *current = base;
    for (int i = 0; i < pointer_level; i++)
    {
        ParsedType *wrapper = cs_allocate_parsed_type();
        wrapper->kind = CS_TYPE_POINTER;
        wrapper->basic_type = CS_BASIC_TYPE_PLUS_ONE;
        wrapper->name_space = CS_TYPE_NAMESPACE_NONE;
        wrapper->name = NULL;
        wrapper->child = current;
        wrapper->array_size = NULL;
        wrapper->is_unsigned = false;
        wrapper->is_const = false;
        current = wrapper;
    }
    return current;
}

ParsedType *cs_wrap_parsed_array(ParsedType *base, Expression *array_size)
{
    ParsedType *wrapper = cs_allocate_parsed_type();
    wrapper->kind = CS_TYPE_ARRAY;
    wrapper->basic_type = CS_BASIC_TYPE_PLUS_ONE;
    wrapper->name_space = CS_TYPE_NAMESPACE_NONE;
    wrapper->name = NULL;
    wrapper->child = base;
    wrapper->array_size = array_size;
    wrapper->is_unsigned = false;
    wrapper->is_const = false;
    return wrapper;
}

ParsedType *cs_copy_parsed_type(const ParsedType *type)
{
    if (!type)
    {
        return NULL;
    }

    ParsedType *copy = cs_allocate_parsed_type();
    copy->kind = type->kind;
    copy->basic_type = type->basic_type;
    copy->name_space = type->name_space;
    copy->name = type->name ? cs_create_identifier(type->name) : NULL;
    copy->child = cs_copy_parsed_type(type->child);
    copy->array_size = type->array_size;
    copy->is_unsigned = type->is_unsigned;
    copy->is_const = type->is_const;
    return copy;
}

void cs_parsed_type_set_unsigned(ParsedType *type, bool is_unsigned)
{
    if (type)
    {
        type->is_unsigned = is_unsigned;
    }
}

void cs_parsed_type_set_const(ParsedType *type, bool is_const)
{
    if (type)
    {
        type->is_const = is_const;
    }
}

/* ============================================================
 * Type Resolution: ParsedType -> TypeSpecifier
 * ============================================================ */

/*
 * Convert ParsedType to TypeSpecifier during semantic analysis.
 *
 * This function:
 * 1. Creates TypeSpecifier structure
 * 2. Resolves typedef names using HeaderStore
 * 3. Looks up struct/union/enum definitions for anonymous types
 * 4. Assigns TypeIdentity properly
 */
TypeSpecifier *cs_resolve_type(const ParsedType *parsed,
                               HeaderStore *store,
                               CS_Compiler *compiler)
{
    if (!parsed)
    {
        return NULL;
    }

    /* Handle pointer types recursively */
    if (parsed->kind == CS_TYPE_POINTER)
    {
        TypeSpecifier *child = cs_resolve_type(parsed->child, store, compiler);
        if (!child)
        {
            return NULL;
        }
        TypeSpecifier *type = cs_wrap_pointer(child, 1);
        cs_type_set_unsigned(type, parsed->is_unsigned);
        cs_type_set_const(type, parsed->is_const);
        return type;
    }

    /* Handle array types recursively */
    if (parsed->kind == CS_TYPE_ARRAY)
    {
        TypeSpecifier *child = cs_resolve_type(parsed->child, store, compiler);
        if (!child)
        {
            return NULL;
        }
        TypeSpecifier *type = cs_wrap_array(child, parsed->array_size);
        cs_type_set_unsigned(type, parsed->is_unsigned);
        cs_type_set_const(type, parsed->is_const);
        return type;
    }

    /* Handle named types (struct, union, enum, typedef) */
    if (parsed->kind == CS_TYPE_NAMED)
    {
        char *name = parsed->name ? cs_create_identifier(parsed->name) : NULL;
        TypeSpecifier *type = cs_create_named_type_specifier(parsed->basic_type, name);
        cs_type_set_unsigned(type, parsed->is_unsigned);
        cs_type_set_const(type, parsed->is_const);

        /* Resolve named types using HeaderIndex (per-TU visibility) */
        HeaderIndex *index = compiler ? compiler->header_index : NULL;
        if (name)
        {
            CS_BasicType basic = parsed->basic_type;

            /* Typedef: resolve to canonical type */
            if (basic == CS_TYPEDEF_NAME)
            {
                TypedefDefinition *td = index ? header_index_find_typedef(index, name) : NULL;
                if (td && td->canonical)
                {
                    return cs_copy_type_specifier(td->canonical);
                }
                /* Typedef not found - return NULL to signal error */
                return NULL;
            }

            /* Struct/Union: validate existence and set class name */
            if (basic == CS_STRUCT_TYPE || basic == CS_UNION_TYPE)
            {
                bool is_union_expected = (basic == CS_UNION_TYPE);
                StructDefinition *sd = index ? header_index_find_struct(index, name) : NULL;
                if (sd && sd->is_union == is_union_expected)
                {
                    /* Use StructDefinition's class name for TypeSpecifier */
                    cs_type_set_user_type_name(type, sd->id.name);
                    return type;
                }
                /* Also check via compiler for local definitions */
                if (compiler)
                {
                    sd = cs_lookup_struct_definition(compiler, name);
                    if (sd && sd->is_union == is_union_expected)
                    {
                        /* Use StructDefinition's class name for TypeSpecifier */
                        cs_type_set_user_type_name(type, sd->id.name);
                        return type;
                    }
                }
                return NULL;
            }

            /* Enum: validate existence and set class name */
            if (basic == CS_ENUM_TYPE)
            {
                EnumDefinition *ed = index ? header_index_find_enum(index, name) : NULL;
                if (ed)
                {
                    /* Use EnumDefinition's class name for TypeSpecifier */
                    cs_type_set_user_type_name(type, ed->id.name);
                    return type;
                }
                /* Also check via compiler for local definitions */
                if (compiler)
                {
                    ed = cs_lookup_enum_definition(compiler, name);
                    if (ed)
                    {
                        /* Use EnumDefinition's class name for TypeSpecifier */
                        cs_type_set_user_type_name(type, ed->id.name);
                        return type;
                    }
                }
                return NULL;
            }
        }

        return type;
    }

    /* Handle basic types (int, char, void, etc.) */
    if (parsed->kind == CS_TYPE_BASIC)
    {
        TypeSpecifier *type = cs_create_type_specifier(parsed->basic_type);
        cs_type_set_unsigned(type, parsed->is_unsigned);
        cs_type_set_const(type, parsed->is_const);
        return type;
    }

    return NULL;
}

/* Resolve a ParsedType using HeaderIndex only (for header parsing) */
TypeSpecifier *cs_resolve_type_with_index(const ParsedType *parsed,
                                          HeaderIndex *index)
{
    if (!parsed)
        return NULL;

    /* Handle pointer types recursively */
    if (parsed->kind == CS_TYPE_POINTER)
    {
        TypeSpecifier *child = cs_resolve_type_with_index(parsed->child, index);
        if (!child)
            return NULL;
        TypeSpecifier *type = cs_wrap_pointer(child, 1);
        cs_type_set_unsigned(type, parsed->is_unsigned);
        cs_type_set_const(type, parsed->is_const);
        return type;
    }

    /* Handle array types recursively */
    if (parsed->kind == CS_TYPE_ARRAY)
    {
        TypeSpecifier *child = cs_resolve_type_with_index(parsed->child, index);
        if (!child)
            return NULL;
        TypeSpecifier *type = cs_wrap_array(child, parsed->array_size);
        cs_type_set_unsigned(type, parsed->is_unsigned);
        cs_type_set_const(type, parsed->is_const);
        return type;
    }

    /* Handle named types (struct, union, enum, typedef) */
    if (parsed->kind == CS_TYPE_NAMED)
    {
        char *name = parsed->name ? cs_create_identifier(parsed->name) : NULL;
        TypeSpecifier *type = cs_create_named_type_specifier(parsed->basic_type, name);
        cs_type_set_unsigned(type, parsed->is_unsigned);
        cs_type_set_const(type, parsed->is_const);

        if (name && index)
        {
            CS_BasicType basic = parsed->basic_type;

            /* Typedef: resolve to canonical type */
            if (basic == CS_TYPEDEF_NAME)
            {
                TypedefDefinition *td = header_index_find_typedef(index, name);
                if (td && td->canonical)
                {
                    return cs_copy_type_specifier(td->canonical);
                }
                return NULL;
            }

            /* Struct/Union: set class name if definition exists */
            if (basic == CS_STRUCT_TYPE || basic == CS_UNION_TYPE)
            {
                StructDefinition *sd = header_index_find_struct(index, name);
                if (sd)
                {
                    /* Use StructDefinition's class name for TypeSpecifier */
                    cs_type_set_user_type_name(type, sd->id.name);
                }
                else
                {
                    /* Cminor requires struct definition to be visible at typedef resolution.
                     * Forward declarations without visible definition will fail at codegen. */
                    fprintf(stderr, "warning: struct '%s' not found (forward declaration)\n", name);
                }
                return type;
            }

            /* Enum: validate existence and set class name */
            if (basic == CS_ENUM_TYPE)
            {
                EnumDefinition *ed = header_index_find_enum(index, name);
                if (ed)
                {
                    /* Use EnumDefinition's class name for TypeSpecifier */
                    cs_type_set_user_type_name(type, ed->id.name);
                    return type;
                }
                return NULL;
            }
        }

        return type;
    }

    /* Handle basic types (int, char, void, etc.) */
    if (parsed->kind == CS_TYPE_BASIC)
    {
        TypeSpecifier *type = cs_create_type_specifier(parsed->basic_type);
        cs_type_set_unsigned(type, parsed->is_unsigned);
        cs_type_set_const(type, parsed->is_const);
        return type;
    }

    return NULL;
}

/* ============================================================
 * Utility Functions
 * ============================================================ */

ParsedType *cs_create_parsed_type_from_specifier(TypeSpecifier *type)
{
    if (!type)
    {
        return NULL;
    }

    ParsedType *parsed = cs_allocate_parsed_type();
    parsed->kind = cs_type_kind(type);
    parsed->basic_type = cs_type_basic_type(type);
    parsed->name_space = CS_TYPE_NAMESPACE_NONE;
    parsed->name = NULL;
    parsed->child = NULL;
    parsed->array_size = NULL;
    parsed->is_unsigned = cs_type_is_unsigned(type);
    parsed->is_const = cs_type_is_const(type);

    if (parsed->kind == CS_TYPE_POINTER || parsed->kind == CS_TYPE_ARRAY)
    {
        parsed->child = cs_create_parsed_type_from_specifier(cs_type_child(type));
        if (parsed->kind == CS_TYPE_ARRAY)
        {
            parsed->array_size = cs_type_array_size(type);
        }
    }
    else if (parsed->kind == CS_TYPE_NAMED)
    {
        if (parsed->basic_type == CS_STRUCT_TYPE)
        {
            parsed->name_space = CS_TYPE_NAMESPACE_STRUCT;
        }
        else if (parsed->basic_type == CS_UNION_TYPE)
        {
            parsed->name_space = CS_TYPE_NAMESPACE_UNION;
        }
        else if (parsed->basic_type == CS_ENUM_TYPE)
        {
            parsed->name_space = CS_TYPE_NAMESPACE_ENUM;
        }
        else if (parsed->basic_type == CS_TYPEDEF_NAME)
        {
            parsed->name_space = CS_TYPE_NAMESPACE_TYPEDEF;
        }

        /* Copy name from TypeIdentity */
        const char *name = cs_type_user_type_name(type);
        if (name)
        {
            parsed->name = cs_create_identifier(name);
        }
    }

    return parsed;
}
