#pragma once

#include "cminor_type.h"

/* Forward declaration */
typedef struct Declaration_tag Declaration;

/*
 * Codegen structures for class generation
 */

/* Static field entry for codegen */
typedef struct CG_StaticField
{
    Declaration *decl;
    TypeSpecifier *type_spec;
} CG_StaticField;

/* Class field definition for codegen */
typedef struct CG_ClassField
{
    char *name;
    TypeSpecifier *type_spec;
} CG_ClassField;

/* Class definition for codegen */
typedef struct CG_ClassDef
{
    char *name;
    CG_ClassField *fields;
    int field_count;
} CG_ClassDef;
