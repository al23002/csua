#pragma once

/* Forward declarations */
typedef struct TypeSpecifier_tag TypeSpecifier;
typedef struct StructMember_tag StructMember;
typedef struct ParameterList_tag ParameterList;
typedef struct AttributeSpecifier_tag AttributeSpecifier;
typedef struct EnumDefinition_tag EnumDefinition;
typedef struct EnumMember_tag EnumMember;
typedef struct StructDefinition_tag StructDefinition;
typedef struct TypedefDefinition_tag TypedefDefinition;
typedef struct TypeIdentity_tag TypeIdentity;
typedef struct FunctionDeclaration_tag FunctionDeclaration;
typedef struct FunctionDeclarationList_tag FunctionDeclarationList;
typedef struct Declaration_tag Declaration;

/*
 * HeaderStore: Collection of file declarations (headers and sources).
 *
 * Each file's declarations are stored separately with index-based access.
 * TypeIdentity (class_name + type_index) can be used to locate types.
 *
 * Design: FileDecl is the authoritative storage for all declarations.
 */

/* Dependency entry for a file */
typedef struct FileDependency_tag
{
    char *path;
    bool is_embedded;
} FileDependency;

/* A single file's declarations (header or source) */
typedef struct FileDecl_tag
{
    char *path;                 /* File path (e.g., "foo.h" or "foo.c") */
    char *class_name;           /* Derived class name (e.g., "Foo") */
    char *corresponding_source; /* For headers: e.g., "foo.c" */
    bool is_header;             /* true if .h, false if .c */

    /* Functions (linked list of FunctionDeclaration) */
    FunctionDeclarationList *functions;

    /* Structs (array for index access) - stores actual StructDefinition */
    StructDefinition **structs;
    int struct_count;
    int struct_capacity;

    /* Typedefs (array for index access) - stores actual TypedefDefinition */
    TypedefDefinition **typedefs;
    int typedef_count;
    int typedef_capacity;

    /* Enums (array for index access) - stores actual EnumDefinition */
    EnumDefinition **enums;
    int enum_count;
    int enum_capacity;

    /* Extern variable declarations (array for index access) */
    Declaration **declarations;
    int declaration_count;
    int declaration_capacity;

    /* Dependencies (headers this file includes) */
    FileDependency *dependencies;
    int dependency_count;
    int dependency_capacity;

    struct FileDecl_tag *next;
} FileDecl;

/* Backwards compatibility alias */
typedef FileDecl HeaderDecl;

/* The file store itself */
typedef struct HeaderStore_tag
{
    FileDecl *files;
} HeaderStore;

/* Lifecycle */
HeaderStore *header_store_create();
void header_store_destroy(HeaderStore *store);

/* File management */
FileDecl *header_store_get_or_create(HeaderStore *store, const char *path);
FileDecl *header_store_find(HeaderStore *store, const char *path);
bool header_store_is_parsed(HeaderStore *store, const char *path);

/* Add function declaration to a file */
void header_decl_add_function(FileDecl *fd, FunctionDeclaration *func);

/* Add struct - returns index, stores StructDefinition directly */
int header_decl_add_struct(FileDecl *fd, StructDefinition *def);

/* Add typedef - stores TypedefDefinition directly */
void header_decl_add_typedef(FileDecl *fd, TypedefDefinition *def);

/* Add enum - returns index, stores EnumDefinition directly */
int header_decl_add_enum(FileDecl *fd, EnumDefinition *def);

/* Add extern variable declaration */
void header_decl_add_declaration(FileDecl *fd, Declaration *decl);

/* Add dependency (header this file includes) */
void file_decl_add_dependency(FileDecl *fd, const char *path, bool is_embedded);

/* Get dependency count and access */
int file_decl_dependency_count(FileDecl *fd);
FileDependency *file_decl_get_dependency(FileDecl *fd, int index);

/* Lookup by name within a file */
StructDefinition *file_decl_find_struct(FileDecl *fd, const char *name);
EnumDefinition *file_decl_find_enum(FileDecl *fd, const char *name);
TypedefDefinition *file_decl_find_typedef(FileDecl *fd, const char *name);
FunctionDeclaration *file_decl_find_function(FileDecl *fd, const char *name);
Declaration *file_decl_find_declaration(FileDecl *fd, const char *name);

/* Forward declaration for HeaderIndex */
typedef struct HeaderIndex_tag HeaderIndex;

/* Resolve typedef types in a FileDecl (first pass).
 * Uses HeaderIndex for per-TU visibility. */
void file_decl_resolve_typedefs(FileDecl *fd, HeaderIndex *index);

/* Resolve struct member and function types in a FileDecl (second pass).
 * Call this after all typedefs have been resolved. */
void file_decl_resolve_struct_types(FileDecl *fd, HeaderIndex *index);
