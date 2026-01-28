#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "header_store.h"
#include "header_index.h"
#include "ast.h"
#include "util.h"
#include "cminor_type.h"
#include "parsed_type.h"
#include "create.h"

static char *dup_string(const char *s)
{
    if (!s)
        return NULL;
    return strdup(s);
}

HeaderStore *header_store_create()
{
    HeaderStore *store = (HeaderStore *)calloc(1, sizeof(HeaderStore));
    return store;
}

void header_store_destroy(HeaderStore *store)
{
    /* No-op: GC handles memory (per CLAUDE.md) */
    (void)store;
}

FileDecl *header_store_find(HeaderStore *store, const char *path)
{
    if (!store || !path)
        return NULL;

    for (FileDecl *fd = store->files; fd; fd = fd->next)
    {
        if (strcmp(fd->path, path) == 0)
        {
            return fd;
        }
    }
    return NULL;
}

bool header_store_is_parsed(HeaderStore *store, const char *path)
{
    return header_store_find(store, path) != NULL;
}

static bool is_header_file(const char *path)
{
    if (!path)
        return false;
    int len = strlen(path);
    return len >= 2 && path[len - 2] == '.' && path[len - 1] == 'h';
}

FileDecl *header_store_get_or_create(HeaderStore *store, const char *path)
{
    if (!store || !path)
        return NULL;

    FileDecl *existing = header_store_find(store, path);
    if (existing)
        return existing;

    FileDecl *fd = (FileDecl *)calloc(1, sizeof(FileDecl));
    fd->path = dup_string(path);
    fd->class_name = cs_class_name_from_path(path);
    fd->is_header = is_header_file(path);
    fd->next = store->files;
    store->files = fd;

    return fd;
}

void header_decl_add_function(FileDecl *fd, FunctionDeclaration *func)
{
    if (!fd || !func)
        return;

    /* Set class_name from FileDecl if not already set */
    if (!func->class_name && fd->class_name)
    {
        func->class_name = dup_string(fd->class_name);
    }

    /* Add to front of linked list */
    FunctionDeclarationList *node = (FunctionDeclarationList *)calloc(1, sizeof(FunctionDeclarationList));
    node->func = func;
    node->next = fd->functions;
    fd->functions = node;
}

static void ensure_struct_capacity(FileDecl *fd, int needed)
{
    if (fd->struct_count + needed <= fd->struct_capacity)
        return;

    int new_cap = fd->struct_capacity == 0 ? 8 : fd->struct_capacity * 2;
    while (new_cap < fd->struct_count + needed)
        new_cap *= 2;

    StructDefinition **new_arr = (StructDefinition **)calloc(new_cap, sizeof(StructDefinition *));
    for (int i = 0; i < fd->struct_count; i++)
        new_arr[i] = fd->structs[i];
    fd->structs = new_arr;
    fd->struct_capacity = new_cap;
}

int header_decl_add_struct(FileDecl *fd, StructDefinition *def)
{
    if (!fd || !def)
        return -1;

    ensure_struct_capacity(fd, 1);

    int index = fd->struct_count;
    fd->structs[fd->struct_count] = def;
    fd->struct_count++;

    return index;
}

static void ensure_typedef_capacity(FileDecl *fd, int needed)
{
    if (fd->typedef_count + needed <= fd->typedef_capacity)
        return;

    int new_cap = fd->typedef_capacity == 0 ? 8 : fd->typedef_capacity * 2;
    while (new_cap < fd->typedef_count + needed)
        new_cap *= 2;

    TypedefDefinition **new_arr = (TypedefDefinition **)calloc(new_cap, sizeof(TypedefDefinition *));
    for (int i = 0; i < fd->typedef_count; i++)
        new_arr[i] = fd->typedefs[i];
    fd->typedefs = new_arr;
    fd->typedef_capacity = new_cap;
}

void header_decl_add_typedef(FileDecl *fd, TypedefDefinition *def)
{
    if (!fd || !def)
        return;

    ensure_typedef_capacity(fd, 1);
    fd->typedefs[fd->typedef_count] = def;
    fd->typedef_count++;
}

static void ensure_enum_capacity(FileDecl *fd, int needed)
{
    if (fd->enum_count + needed <= fd->enum_capacity)
        return;

    int new_cap = fd->enum_capacity == 0 ? 8 : fd->enum_capacity * 2;
    while (new_cap < fd->enum_count + needed)
        new_cap *= 2;

    EnumDefinition **new_arr = (EnumDefinition **)calloc(new_cap, sizeof(EnumDefinition *));
    for (int i = 0; i < fd->enum_count; i++)
        new_arr[i] = fd->enums[i];
    fd->enums = new_arr;
    fd->enum_capacity = new_cap;
}

int header_decl_add_enum(FileDecl *fd, EnumDefinition *def)
{
    if (!fd || !def)
        return -1;

    ensure_enum_capacity(fd, 1);

    int index = fd->enum_count;
    fd->enums[fd->enum_count] = def;
    fd->enum_count++;

    return index;
}

static void ensure_declaration_capacity(FileDecl *fd, int needed)
{
    if (fd->declaration_count + needed <= fd->declaration_capacity)
        return;

    int new_cap = fd->declaration_capacity == 0 ? 8 : fd->declaration_capacity * 2;
    while (new_cap < fd->declaration_count + needed)
        new_cap *= 2;

    Declaration **new_arr = (Declaration **)calloc(new_cap, sizeof(Declaration *));
    for (int i = 0; i < fd->declaration_count; i++)
        new_arr[i] = fd->declarations[i];
    fd->declarations = new_arr;
    fd->declaration_capacity = new_cap;
}

void header_decl_add_declaration(FileDecl *fd, Declaration *decl)
{
    if (!fd || !decl)
        return;

    /* Set class_name from FileDecl if not already set */
    if (!decl->class_name && fd->class_name)
    {
        decl->class_name = dup_string(fd->class_name);
    }

    ensure_declaration_capacity(fd, 1);
    fd->declarations[fd->declaration_count] = decl;
    fd->declaration_count++;
}

/* Lookup by name within a file.
 * For named types: matches search_name (e.g., "Preprocessor")
 * For anonymous types: matches name (e.g., "foo$0")
 * Also matches by name for internal lookups (e.g., "preprocessor_h$Preprocessor")
 */
StructDefinition *file_decl_find_struct(FileDecl *fd, const char *name)
{
    if (!fd || !name)
        return NULL;

    for (int i = 0; i < fd->struct_count; i++)
    {
        StructDefinition *def = fd->structs[i];
        /* Match by search_name (original C name) if available */
        if (def->id.search_name && strcmp(def->id.search_name, name) == 0)
        {
            return def;
        }
        /* Match by name (class name) for anonymous or internal lookups */
        if (def->id.name && strcmp(def->id.name, name) == 0)
        {
            return def;
        }
    }
    return NULL;
}

EnumDefinition *file_decl_find_enum(FileDecl *fd, const char *name)
{
    if (!fd || !name)
        return NULL;

    for (int i = 0; i < fd->enum_count; i++)
    {
        EnumDefinition *def = fd->enums[i];
        /* Match by search_name (original C name) if available */
        if (def->id.search_name && strcmp(def->id.search_name, name) == 0)
        {
            return def;
        }
        /* Match by name (class name) for anonymous or internal lookups */
        if (def->id.name && strcmp(def->id.name, name) == 0)
        {
            return def;
        }
    }
    return NULL;
}

TypedefDefinition *file_decl_find_typedef(FileDecl *fd, const char *name)
{
    if (!fd || !name)
        return NULL;

    for (int i = 0; i < fd->typedef_count; i++)
    {
        TypedefDefinition *def = fd->typedefs[i];
        if (def->name && strcmp(def->name, name) == 0)
        {
            return def;
        }
    }
    return NULL;
}

FunctionDeclaration *file_decl_find_function(FileDecl *fd, const char *name)
{
    if (!fd || !name)
        return NULL;

    for (FunctionDeclarationList *fl = fd->functions; fl; fl = fl->next)
    {
        FunctionDeclaration *f = fl->func;
        if (f && f->name && strcmp(f->name, name) == 0)
        {
            return f;
        }
    }
    return NULL;
}

Declaration *file_decl_find_declaration(FileDecl *fd, const char *name)
{
    if (!fd || !name)
        return NULL;

    for (int i = 0; i < fd->declaration_count; i++)
    {
        Declaration *decl = fd->declarations[i];
        if (decl->name && strcmp(decl->name, name) == 0)
        {
            return decl;
        }
    }
    return NULL;
}

static void ensure_dependency_capacity(FileDecl *fd, int needed)
{
    if (fd->dependency_count + needed <= fd->dependency_capacity)
        return;

    int new_cap = fd->dependency_capacity == 0 ? 8 : fd->dependency_capacity * 2;
    while (new_cap < fd->dependency_count + needed)
        new_cap *= 2;

    FileDependency *new_arr = (FileDependency *)calloc(new_cap, sizeof(FileDependency));
    for (int i = 0; i < fd->dependency_count; i++)
        new_arr[i] = fd->dependencies[i];
    fd->dependencies = new_arr;
    fd->dependency_capacity = new_cap;
}

void file_decl_add_dependency(FileDecl *fd, const char *path, bool is_embedded)
{
    if (!fd || !path)
        return;

    /* Check for duplicates */
    for (int i = 0; i < fd->dependency_count; i++)
    {
        if (strcmp(fd->dependencies[i].path, path) == 0)
            return;
    }

    ensure_dependency_capacity(fd, 1);
    fd->dependencies[fd->dependency_count].path = dup_string(path);
    fd->dependencies[fd->dependency_count].is_embedded = is_embedded;
    fd->dependency_count++;
}

int file_decl_dependency_count(FileDecl *fd)
{
    return fd ? fd->dependency_count : 0;
}

FileDependency *file_decl_get_dependency(FileDecl *fd, int index)
{
    if (!fd || index < 0 || index >= fd->dependency_count)
        return NULL;
    return &fd->dependencies[index];
}

/* Resolve typedef types in a FileDecl (first pass) */
void file_decl_resolve_typedefs(FileDecl *fd, HeaderIndex *index)
{
    if (!fd || !index)
        return;

    for (int i = 0; i < fd->typedef_count; i++)
    {
        TypedefDefinition *def = fd->typedefs[i];

        /* Resolve type if not yet resolved */
        if (def->parsed_type && !def->type)
        {
            def->type = cs_resolve_type_with_index(def->parsed_type, index);
            if (def->type)
            {
                cs_type_set_typedef(def->type, true);
            }
        }

        /* Resolve canonical type for typedef chains */
        if (!def->canonical && def->type)
        {
            if (cs_type_basic_type(def->type) == CS_TYPEDEF_NAME)
            {
                const char *alias_name = cs_type_user_type_name(def->type);
                if (alias_name)
                {
                    TypedefDefinition *td = header_index_find_typedef(index, alias_name);
                    if (td && td->canonical)
                    {
                        def->canonical = cs_copy_type_specifier(td->canonical);
                        if (cs_type_is_unsigned(def->type))
                        {
                            cs_type_set_unsigned(def->canonical, true);
                        }
                    }
                }
            }
            if (!def->canonical)
            {
                def->canonical = def->type;
            }
        }
    }
}

/* Resolve struct member and function types in a FileDecl (second pass) */
void file_decl_resolve_struct_types(FileDecl *fd, HeaderIndex *index)
{
    if (!fd || !index)
        return;

    /* Resolve struct member types */
    for (int i = 0; i < fd->struct_count; i++)
    {
        StructDefinition *def = fd->structs[i];
        for (StructMember *m = def->members; m; m = m->next)
        {
            if (!m->type && m->parsed_type)
            {
                m->type = cs_resolve_type_with_index(m->parsed_type, index);
            }
        }
    }

    /* Resolve function return and parameter types */
    for (FunctionDeclarationList *fl = fd->functions; fl; fl = fl->next)
    {
        FunctionDeclaration *f = fl->func;
        if (!f)
            continue;

        /* Resolve return type if not yet resolved */
        if (!f->type && f->parsed_type)
        {
            f->type = cs_resolve_type_with_index(f->parsed_type, index);
        }

        /* Resolve parameter types */
        for (ParameterList *p = f->param; p; p = p->next)
        {
            if (p->is_ellipsis)
                break;
            if (!p->type && p->parsed_type)
            {
                p->type = cs_resolve_type_with_index(p->parsed_type, index);
            }
        }
    }
}
