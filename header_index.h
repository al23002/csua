#pragma once

/*
 * HeaderIndex: Per-translation-unit index of visible declarations.
 *
 * HeaderStore holds all parsed FileDecls (persistent).
 * HeaderIndex holds references to FileDecls visible in current TU (ephemeral).
 *
 * Each translation unit creates a new HeaderIndex, so declarations
 * from one TU don't leak into another.
 */

#include "header_store.h"

typedef struct HeaderIndex_tag
{
    FileDecl **files; /* Array of visible FileDecl pointers */
    int file_count;
    int file_capacity;
} HeaderIndex;

/* Lifecycle */
HeaderIndex *header_index_create();

/* Add a FileDecl to the visible set */
void header_index_add_file(HeaderIndex *index, FileDecl *fd);

/* Check if a file is already in the index */
bool header_index_contains(HeaderIndex *index, FileDecl *fd);

/* Lookup by name (searches only visible files) */
StructDefinition *header_index_find_struct(HeaderIndex *index, const char *name);
StructDefinition *header_index_find_struct_with_file(HeaderIndex *index, const char *name,
                                                     FileDecl **out_fd);
EnumDefinition *header_index_find_enum(HeaderIndex *index, const char *name);
EnumDefinition *header_index_find_enum_with_file(HeaderIndex *index, const char *name,
                                                 FileDecl **out_fd);
TypedefDefinition *header_index_find_typedef(HeaderIndex *index, const char *name);
FunctionDeclaration *header_index_find_function(HeaderIndex *index, const char *name);
Declaration *header_index_find_declaration(HeaderIndex *index, const char *name);
EnumMember *header_index_find_enum_member(HeaderIndex *index,
                                          const char *member_name,
                                          EnumDefinition **out_enum);
