#pragma once

/*
 * definitions.h - Type definition structures
 *
 * Contains:
 * - EnumMember, EnumDefinition: Enum type definitions
 * - StructMember, StructDefinition: Struct/union type definitions
 * - TypedefDefinition: Typedef definitions
 */

#include "cminor_base.h"
#include "type_specifier.h"
#include "parsed_type.h"

/* Enum member (constant) within an enum definition */
typedef struct EnumMember_tag
{
    char *name;
    int value; /* User-specified or auto (0, prev+1) */
    bool has_explicit_value;
    struct EnumDefinition_tag *enum_def; /* Back-reference to parent enum */
    struct EnumMember_tag *next;
} EnumMember;

/* Enum definition (like struct definition) */
typedef struct EnumDefinition_tag
{
    TypeIdentity id;     /* Type identification */
    EnumMember *members; /* Linked list of members */
    int member_count;    /* Number of members */
    struct EnumDefinition_tag *next;
} EnumDefinition;

typedef struct StructMember_tag
{
    char *name;
    TypeSpecifier *type;
    ParsedType *parsed_type;
    struct StructMember_tag *next;
} StructMember;

typedef struct StructDefinition_tag
{
    TypeIdentity id; /* Type identification */
    StructMember *members;
    bool is_union;
    struct StructDefinition_tag *next;
} StructDefinition;

typedef struct TypedefDefinition_tag
{
    char *name;
    ParsedType *parsed_type;  /* Parsed type from parser (unresolved) */
    TypeSpecifier *type;      /* Resolved type (set during semantic analysis) */
    TypeSpecifier *canonical; /* Resolved base type */
    char *source_path;
    struct TypedefDefinition_tag *next;
} TypedefDefinition;

/* Enum definition API */
void cs_register_enum_definition(CS_Compiler *compiler, char *name, EnumMember *members);
EnumMember *cs_lookup_enum_member(CS_Compiler *compiler, const char *name);
EnumDefinition *cs_lookup_enum_definition(CS_Compiler *compiler, const char *name);

/* Struct definition API */
StructMember *cs_create_struct_member(ParsedType *type, char *name);
StructMember *cs_chain_struct_member(StructMember *list, StructMember *member_list);
StructDefinition *cs_register_struct_definition(CS_Compiler *compiler, char *name,
                                                StructMember *members, bool is_union);
StructDefinition *cs_lookup_struct_definition(CS_Compiler *compiler,
                                              const char *name);
StructMember *cs_lookup_struct_member(CS_Compiler *compiler, TypeSpecifier *type,
                                      const char *member_name);
