#include <stdlib.h>
#include <string.h>
#include "header_index.h"
#include "ast.h"
#include "definitions.h"

#define INITIAL_CAPACITY 8

HeaderIndex *header_index_create()
{
    HeaderIndex *index = (HeaderIndex *)calloc(1, sizeof(HeaderIndex));
    index->files = (FileDecl **)calloc(INITIAL_CAPACITY, sizeof(FileDecl *));
    index->file_count = 0;
    index->file_capacity = INITIAL_CAPACITY;
    return index;
}

void header_index_add_file(HeaderIndex *index, FileDecl *fd)
{
    if (!index || !fd)
        return;

    /* Check if already added */
    if (header_index_contains(index, fd))
        return;

    /* Grow array if needed */
    if (index->file_count >= index->file_capacity)
    {
        int new_capacity = index->file_capacity * 2;
        FileDecl **new_files = (FileDecl **)calloc(new_capacity, sizeof(FileDecl *));
        for (int i = 0; i < index->file_count; i++)
        {
            new_files[i] = index->files[i];
        }
        index->files = new_files;
        index->file_capacity = new_capacity;
    }

    index->files[index->file_count++] = fd;
}

bool header_index_contains(HeaderIndex *index, FileDecl *fd)
{
    if (!index || !fd)
        return false;

    for (int i = 0; i < index->file_count; i++)
    {
        if (index->files[i] == fd)
            return true;
    }
    return false;
}

StructDefinition *header_index_find_struct(HeaderIndex *index, const char *name)
{
    return header_index_find_struct_with_file(index, name, NULL);
}

StructDefinition *header_index_find_struct_with_file(HeaderIndex *index, const char *name,
                                                     FileDecl **out_fd)
{
    if (!index || !name)
        return NULL;

    for (int i = 0; i < index->file_count; i++)
    {
        StructDefinition *sd = file_decl_find_struct(index->files[i], name);
        if (sd)
        {
            if (out_fd)
                *out_fd = index->files[i];
            return sd;
        }
    }
    return NULL;
}

EnumDefinition *header_index_find_enum(HeaderIndex *index, const char *name)
{
    return header_index_find_enum_with_file(index, name, NULL);
}

EnumDefinition *header_index_find_enum_with_file(HeaderIndex *index, const char *name,
                                                 FileDecl **out_fd)
{
    if (!index || !name)
        return NULL;

    for (int i = 0; i < index->file_count; i++)
    {
        EnumDefinition *ed = file_decl_find_enum(index->files[i], name);
        if (ed)
        {
            if (out_fd)
                *out_fd = index->files[i];
            return ed;
        }
    }
    return NULL;
}

TypedefDefinition *header_index_find_typedef(HeaderIndex *index, const char *name)
{
    if (!index || !name)
        return NULL;

    for (int i = 0; i < index->file_count; i++)
    {
        TypedefDefinition *td = file_decl_find_typedef(index->files[i], name);
        if (td)
            return td;
    }
    return NULL;
}

FunctionDeclaration *header_index_find_function(HeaderIndex *index, const char *name)
{
    if (!index || !name)
        return NULL;

    for (int i = 0; i < index->file_count; i++)
    {
        FunctionDeclaration *f = file_decl_find_function(index->files[i], name);
        if (f)
            return f;
    }
    return NULL;
}

Declaration *header_index_find_declaration(HeaderIndex *index, const char *name)
{
    if (!index || !name)
        return NULL;

    for (int i = 0; i < index->file_count; i++)
    {
        Declaration *d = file_decl_find_declaration(index->files[i], name);
        if (d)
            return d;
    }
    return NULL;
}

EnumMember *header_index_find_enum_member(HeaderIndex *index,
                                          const char *member_name,
                                          EnumDefinition **out_enum)
{
    if (!index || !member_name)
        return NULL;

    for (int i = 0; i < index->file_count; i++)
    {
        FileDecl *fd = index->files[i];
        for (int j = 0; j < fd->enum_count; j++)
        {
            EnumDefinition *ed = fd->enums[j];
            if (!ed)
                continue;
            for (EnumMember *m = ed->members; m; m = m->next)
            {
                if (m->name && strcmp(m->name, member_name) == 0)
                {
                    if (out_enum)
                        *out_enum = ed;
                    return m;
                }
            }
        }
    }
    return NULL;
}
