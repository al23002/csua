/*
 * definitions.c - Type definition operations
 *
 * Implementation of enum, struct, and typedef definition functions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "definitions.h"
#include "compiler.h"
#include "cminor_type.h"
#include "header_store.h"
#include "header_index.h"

/* ============================================================
 * Struct Member Creation
 * ============================================================ */

StructMember *cs_create_struct_member(ParsedType *type, char *name)
{
    StructMember *member = (StructMember *)calloc(1, sizeof(StructMember));
    member->name = name;
    member->type = NULL;
    member->parsed_type = cs_copy_parsed_type(type);
    member->next = NULL;
    return member;
}

StructMember *cs_chain_struct_member(StructMember *list,
                                     StructMember *member_list)
{
    if (list == NULL)
    {
        return member_list;
    }
    if (member_list == NULL)
    {
        return list;
    }
    StructMember *tail = list;
    while (tail->next)
    {
        tail = tail->next;
    }
    tail->next = member_list;
    return list;
}

/* ============================================================
 * Enum Definition
 * ============================================================ */

/* Register enum definition with compiler, assigning values.
 * Anonymous enums get generated names like "ClassName$index". */
void cs_register_enum_definition(CS_Compiler *compiler, char *name, EnumMember *members)
{
    if (compiler == NULL)
    {
        return;
    }
    EnumDefinition *def = (EnumDefinition *)calloc(1, sizeof(EnumDefinition));

    /* Generate name for anonymous enums: "ClassName$index" */
    if (name == NULL)
    {
        int idx = compiler->enum_type_counter++;
        const char *class_name = compiler->current_file_decl ? compiler->current_file_decl->class_name : NULL;
        char buf[256];
        snprintf(buf, sizeof buf, "%s$%d", class_name ? class_name : "anon", idx);
        def->id.name = strdup(buf);
        compiler->last_anon_enum_def = def;
    }
    else
    {
        /* Named enum: generate qualified class name "ClassName_h$EnumName"
         * search_name holds the original C name for lookups */
        def->id.search_name = strdup(name);
        const char *class_name = compiler->current_file_decl ? compiler->current_file_decl->class_name : NULL;
        bool is_header = false;
        if (compiler->current_file_decl)
        {
            const char *path = compiler->current_file_decl->path;
            if (path)
            {
                int len = strlen(path);
                is_header = (len > 2 && strcmp(path + len - 2, ".h") == 0);
            }
        }
        char buf[256];
        snprintf(buf, sizeof buf, "%s%s$%s",
                 class_name ? class_name : "anon",
                 is_header ? "_h" : "",
                 name);
        def->id.name = strdup(buf);
        compiler->enum_type_counter++;
        compiler->last_anon_enum_def = NULL;
    }
    def->members = members;
    def->member_count = 0;
    def->next = NULL;

    /* Add to current file's enum list */
    if (compiler->current_file_decl)
    {
        header_decl_add_enum(compiler->current_file_decl, def);
    }

    /* Assign values: first=0, subsequent=prev+1, or use explicit value */
    int next_value = 0;
    for (EnumMember *m = members; m; m = m->next)
    {
        if (!m->has_explicit_value)
        {
            m->value = next_value;
        }
        next_value = m->value + 1;
        m->enum_def = def;
        def->member_count++;
    }
}

EnumMember *cs_lookup_enum_member(CS_Compiler *compiler, const char *name)
{
    if (name == NULL || compiler == NULL || !compiler->header_index)
    {
        return NULL;
    }
    return header_index_find_enum_member(compiler->header_index, name, NULL);
}

EnumDefinition *cs_lookup_enum_definition(CS_Compiler *compiler, const char *name)
{
    if (name == NULL || compiler == NULL || !compiler->header_index)
    {
        return NULL;
    }
    return header_index_find_enum(compiler->header_index, name);
}

/* ============================================================
 * Struct Definition
 * ============================================================ */

static TypeSpecifier *descend_to_struct(TypeSpecifier *type)
{
    TypeSpecifier *current = type;
    while (current &&
           (cs_type_is_pointer(current) || cs_type_is_array(current)))
    {
        current = cs_type_child(current);
    }
    return current;
}

static TypeSpecifier *ensure_struct_member_type(StructMember *member)
{
    if (member == NULL)
    {
        return NULL;
    }
    /* member->type should already be resolved during struct registration */
    return member->type;
}

StructDefinition *cs_lookup_struct_definition(CS_Compiler *compiler,
                                              const char *name)
{
    if (compiler == NULL || name == NULL)
    {
        return NULL;
    }
    /* Search in current file's structs */
    FileDecl *fd = compiler->current_file_decl;
    if (fd)
    {
        StructDefinition *def = file_decl_find_struct(fd, name);
        if (def)
            return def;
    }
    /* Search in visible files via header index */
    if (compiler->header_index)
    {
        StructDefinition *def = header_index_find_struct(compiler->header_index, name);
        if (def)
            return def;
    }
    return NULL;
}

StructDefinition *cs_register_struct_definition(CS_Compiler *compiler, char *name,
                                                StructMember *members, bool is_union)
{
    if (compiler == NULL)
    {
        return NULL;
    }
    for (StructMember *m = members; m; m = m->next)
    {
        ensure_struct_member_type(m);
    }
    /* Check for existing definition only if named */
    if (name)
    {
        StructDefinition *def = cs_lookup_struct_definition(compiler, name);
        if (def)
        {
            /* Error if both definitions have members (conflicting definitions) */
            if (def->members && members)
            {
                fprintf(stderr, "Error: Duplicate struct/union definition '%s' with members\n", name);
                exit(1);
            }
            /* Only overwrite members if existing definition has none */
            if (!def->members && members)
            {
                def->members = members;
            }
            def->is_union = is_union;
            return def;
        }
    }
    StructDefinition *node = (StructDefinition *)calloc(1, sizeof(StructDefinition));

    /* Generate name for anonymous structs/unions: "ClassName_h$index" or "ClassName$index"
     * Include "_h" suffix for header files to avoid collision with .c file structs */
    if (name == NULL)
    {
        int idx = compiler->struct_type_counter++;
        const char *class_name = compiler->current_file_decl ? compiler->current_file_decl->class_name : NULL;
        /* Check if this is a header file by looking at current_file_decl path */
        bool is_header = false;
        if (compiler->current_file_decl)
        {
            const char *path = compiler->current_file_decl->path;
            if (path)
            {
                int len = strlen(path);
                is_header = (len > 2 && strcmp(path + len - 2, ".h") == 0);
            }
        }
        char buf[256];
        snprintf(buf, sizeof buf, "%s%s$%d",
                 class_name ? class_name : "anon",
                 is_header ? "_h" : "",
                 idx);
        node->id.name = strdup(buf);
        compiler->last_anon_struct_def = node;
    }
    else
    {
        /* Named struct: generate qualified class name "ClassName_h$StructName"
         * search_name holds the original C name for lookups */
        node->id.search_name = name;
        const char *class_name = compiler->current_file_decl ? compiler->current_file_decl->class_name : NULL;
        bool is_header = false;
        if (compiler->current_file_decl)
        {
            const char *path = compiler->current_file_decl->path;
            if (path)
            {
                int len = strlen(path);
                is_header = (len > 2 && strcmp(path + len - 2, ".h") == 0);
            }
        }
        char buf[256];
        snprintf(buf, sizeof buf, "%s%s$%s",
                 class_name ? class_name : "anon",
                 is_header ? "_h" : "",
                 name);
        node->id.name = strdup(buf);
        compiler->struct_type_counter++;
        compiler->last_anon_struct_def = NULL;
    }
    node->members = members;
    node->is_union = is_union;
    node->next = NULL;

    /* Add to current file's struct list */
    if (compiler->current_file_decl)
    {
        header_decl_add_struct(compiler->current_file_decl, node);
    }

    return node;
}

StructMember *cs_lookup_struct_member(CS_Compiler *compiler, TypeSpecifier *type,
                                      const char *member_name)
{
    if (type == NULL || member_name == NULL)
    {
        return NULL;
    }
    TypeSpecifier *struct_type = descend_to_struct(type);
    if (struct_type == NULL)
    {
        return NULL;
    }

    /* Resolve typedef to get canonical struct type */
    TypeSpecifier *resolved = cs_type_canonical(struct_type, compiler ? compiler->header_index : NULL);
    if (resolved && resolved != struct_type)
    {
        struct_type = resolved;
    }

    StructMember *members = cs_type_struct_members(struct_type);
    if (members == NULL)
    {
        /* All structs now have names (including anonymous ones like "Foo_h$0") */
        const char *user_name = cs_type_user_type_name(struct_type);
        if (user_name)
        {
            StructDefinition *def = cs_lookup_struct_definition(compiler, user_name);
            if (def)
            {
                members = def->members;
                cs_type_set_struct_members(struct_type, members);
            }
        }
    }
    for (StructMember *m = members; m; m = m->next)
    {
        if (strcmp(m->name, member_name) == 0)
        {
            ensure_struct_member_type(m);
            return m;
        }
    }
    return NULL;
}
