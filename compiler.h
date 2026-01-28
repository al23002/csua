#pragma once

/*
 * compiler.h - Compiler infrastructure definitions
 *
 * Architecture:
 * - CompilerContext: Global state shared across all translation units
 * - TranslationUnit: Per-file state (created fresh for each .c file)
 * - CS_Compiler: Alias for TranslationUnit (for parser compatibility)
 */

#include "cminor_base.h"
#include "header_index.h"

/* Pending dependency for compilation */
typedef struct CS_PendingDependency_tag
{
    char *path;
    bool is_embedded;
    struct CS_PendingDependency_tag *next;
} CS_PendingDependency;

/* Forward declare FileDecl */
typedef struct FileDecl_tag FileDecl;

/*
 * CompilerContext: Global state shared across all translation units
 */
typedef struct CompilerContext_tag
{
    HeaderStore *header_store;             /* Persistent storage for all declarations */
    CS_PendingDependency *pending_sources; /* Source files to compile */
    CS_PendingDependency *compiled_deps;   /* Already compiled dependencies */

    /* Aggregated from all translation units (for mean_check and codegen)
     * Note: functions are stored in FileDecl->functions directly */
    StatementList *all_statements;
    DeclarationList *all_declarations;
} CompilerContext;

/*
 * TranslationUnit: Per-file state (pure, no leakage between files)
 */
typedef struct TranslationUnit_tag
{
    CompilerContext *ctx; /* Parent context (shared) */

    /* Shortcuts to ctx fields (for compatibility with existing code) */
    HeaderStore *header_store; /* = ctx->header_store */
    HeaderIndex *header_index; /* Per-TU index of visible declarations */

    /* Parse temporaries */
    StatementList *stmt_list;
    DeclarationList *decl_list;

    /* Translation unit local */
    FileDecl *current_file_decl;

    /* Per-file counters */
    int enum_type_counter;
    int struct_type_counter;
    struct EnumDefinition_tag *last_anon_enum_def;
    struct StructDefinition_tag *last_anon_struct_def;
} TranslationUnit;

/* CS_Creator: Context for creating AST nodes */
typedef struct CS_Creator_tag
{
    int line_number;
    const char *source_path;
    TranslationUnit *tu;
} CS_Creator;

typedef struct CS_ScannerConfig
{
    const char *source_path;
    const unsigned char *input_bytes;
    int input_size;
    TranslationUnit *tu;
} CS_ScannerConfig;

/* compiler.c - Context management */
CompilerContext *compiler_context_create();
void compiler_context_destroy(CompilerContext *ctx);

/* compiler.c - Translation unit management */
TranslationUnit *tu_create(CompilerContext *ctx, const char *source_path);

/* compiler.c - Main API */
bool CS_compile(CompilerContext *ctx, const char *path, bool is_embedded);
void cs_add_runtime_dependency(CompilerContext *ctx, const char *header_name);

/* Compatibility macro: parser uses 'compiler' but we pass TranslationUnit */
#define compiler tu
