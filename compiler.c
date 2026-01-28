#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "compiler.h"
#include "util.h"
#include "scanner.h"
#include "embedded_data.h"
#include "header_decl_visitor.h"
#include "header_store.h"
#include "definitions.h"
#include "meanvisitor.h"
#include "parsed_type.h"
#include "parser.h"

static int mean_debug = 0;
#define DBG_PRINT(...) \
    if (mean_debug)    \
    fprintf(stderr, __VA_ARGS__)

CompilerContext *compiler_context_create()
{
    CompilerContext *ctx = (CompilerContext *)calloc(1, sizeof(CompilerContext));
    ctx->header_store = header_store_create();
    ctx->pending_sources = NULL;
    ctx->compiled_deps = NULL;
    return ctx;
}

void compiler_context_destroy(CompilerContext *ctx)
{
    if (!ctx)
        return;
    /* header_store is GC'd per CLAUDE.md */
    free(ctx);
}

TranslationUnit *tu_create(CompilerContext *ctx, const char *source_path)
{
    TranslationUnit *tu = (TranslationUnit *)calloc(1, sizeof(TranslationUnit));
    tu->ctx = ctx;
    tu->header_store = ctx ? ctx->header_store : NULL; /* Shortcut for compatibility */
    tu->header_index = header_index_create();          /* Per-TU index of visible declarations */
    tu->stmt_list = NULL;
    tu->decl_list = NULL;
    tu->current_file_decl = NULL;
    tu->enum_type_counter = 0;
    tu->struct_type_counter = 0;
    tu->last_anon_enum_def = NULL;
    tu->last_anon_struct_def = NULL;
    return tu;
}

static void free_dependency_list(CS_PendingDependency *list)
{
    while (list)
    {
        CS_PendingDependency *next = list->next;
        free(list->path);
        free(list);
        list = next;
    }
}

static void tu_destroy(TranslationUnit *tu)
{
    if (!tu)
        return;
    free(tu);
}

/* Per-translation-unit mean_check.
 * tu->header_index must already be populated with source file and its headers.
 * Other .c files are NOT visible - this enforces translation unit isolation. */
static bool do_mean_check_for_tu(TranslationUnit *tu, FileDecl *source_file)
{
    DBG_PRINT("DEBUG: do_mean_check_for_tu start for %s\n",
              source_file->class_name ? source_file->class_name : "(null)");

    /* header_index is already populated by process_dependencies */
    tu->current_file_decl = source_file;

    /* Resolve types for visible files only (uses header_index for per-TU visibility) */
    for (int i = 0; i < tu->header_index->file_count; i++)
    {
        FileDecl *fd = tu->header_index->files[i];
        file_decl_resolve_typedefs(fd, tu->header_index);
    }
    for (int i = 0; i < tu->header_index->file_count; i++)
    {
        FileDecl *fd = tu->header_index->files[i];
        file_decl_resolve_struct_types(fd, tu->header_index);
    }

    MeanVisitor *mean_visitor = create_mean_visitor(tu);

    /* Traverse statements in this TU */
    StatementList *stmt_list = tu->stmt_list;
    while (stmt_list)
    {
        if (stmt_list->stmt)
        {
            mean_traverse_stmt(stmt_list->stmt, mean_visitor);
        }
        stmt_list = stmt_list->next;
    }

    /* Traverse functions in this source file only */
    DBG_PRINT("DEBUG: traversing functions in %s\n",
              source_file->class_name ? source_file->class_name : "(null)");
    for (FunctionDeclarationList *fl = source_file->functions; fl; fl = fl->next)
    {
        FunctionDeclaration *func = fl->func;
        if (!func)
            continue;

        DBG_PRINT("DEBUG: traversing function %s\n", func->name);
        if (func->body)
        {
            mean_visitor_enter_function(mean_visitor, func);
            mean_traverse_stmt(func->body, mean_visitor);
            mean_visitor_leave_function(mean_visitor);
        }
    }
    DBG_PRINT("DEBUG: traversing functions done\n");

    /* Assign indices to declarations in this TU */
    DeclarationList *dp = tu->decl_list;
    for (int i = 0; dp; dp = dp->next, ++i)
    {
        dp->decl->index = i;
    }

    if (mean_visitor->check_log != NULL)
    {
        show_mean_error(mean_visitor);
        delete_visitor((Visitor *)mean_visitor);
        return false;
    }
    else
    {
        delete_visitor((Visitor *)mean_visitor);
        return true;
    }
}

/* Normalize path by stripping leading "./" */
static const char *normalize_path(const char *path)
{
    while (path[0] == '.' && path[1] == '/')
    {
        path += 2;
    }
    return path;
}

/* Check if a file is in the dependency list */
static bool is_in_dependency_list(CS_PendingDependency *list, const char *path,
                                  bool is_embedded)
{
    const char *normalized = normalize_path(path);
    for (CS_PendingDependency *dep = list; dep; dep = dep->next)
    {
        const char *dep_normalized = normalize_path(dep->path);
        if (dep->is_embedded == is_embedded && strcmp(dep_normalized, normalized) == 0)
            return true;
    }
    return false;
}

/* Add path to compiled list */
static void mark_as_compiled(CompilerContext *ctx, const char *path, bool is_embedded)
{
    CS_PendingDependency *dep = (CS_PendingDependency *)calloc(1, sizeof(CS_PendingDependency));
    dep->path = strdup(path);
    dep->is_embedded = is_embedded;
    dep->next = ctx->compiled_deps;
    ctx->compiled_deps = dep;
}

/* Check if path ends with .h */
static bool is_header_path(const char *path);

/* Forward declaration */
static bool parse_header_internal(CompilerContext *ctx, const char *header_path, bool is_embedded,
                                  CS_PendingDependency **pending_headers_out);

static bool is_header_path(const char *path)
{
    int len = strlen(path);
    return (len > 2 && strcmp(path + len - 2, ".h") == 0);
}

/* Add source to pending_sources if not already compiled/pending */
static void add_pending_source(CompilerContext *ctx, const char *path, bool is_embedded)
{
    if (is_in_dependency_list(ctx->compiled_deps, path, is_embedded))
        return;
    if (is_in_dependency_list(ctx->pending_sources, path, is_embedded))
        return;

    DBG_PRINT("[add_source] %s (embedded=%d)\n", path, is_embedded);
    CS_PendingDependency *dep = (CS_PendingDependency *)calloc(1, sizeof(CS_PendingDependency));
    dep->path = strdup(path);
    dep->is_embedded = is_embedded;
    dep->next = ctx->pending_sources;
    ctx->pending_sources = dep;
}

/* Add header to a local pending list (not the global pending_sources) */
static void add_pending_header_local(CS_PendingDependency **list_ptr,
                                     const char *path, bool is_embedded)
{
    /* Check for duplicates */
    const char *normalized = normalize_path(path);
    for (CS_PendingDependency *dep = *list_ptr; dep; dep = dep->next)
    {
        const char *dep_normalized = normalize_path(dep->path);
        if (dep->is_embedded == is_embedded && strcmp(dep_normalized, normalized) == 0)
            return;
    }

    CS_PendingDependency *dep = (CS_PendingDependency *)calloc(1, sizeof(CS_PendingDependency));
    dep->path = strdup(path);
    dep->is_embedded = is_embedded;
    dep->next = *list_ptr;
    *list_ptr = dep;
}

/* Pop from local pending list */
static CS_PendingDependency *pop_pending_header_local(CS_PendingDependency **list_ptr)
{
    CS_PendingDependency *dep = *list_ptr;
    if (dep)
    {
        *list_ptr = dep->next;
        dep->next = NULL;
    }
    return dep;
}

/* Collect dependencies from scanner into local lists.
 * Headers go to pending_headers, sources go to ctx->pending_sources. */
static void collect_dependencies_to_lists(CompilerContext *ctx, Scanner *scanner,
                                          CS_PendingDependency **pending_headers)
{
    int count = cs_scanner_dependency_count(scanner);
    for (int i = 0; i < count; ++i)
    {
        const char *path = cs_scanner_dependency_path(scanner, i);
        bool is_embedded = cs_scanner_dependency_is_embedded(scanner, i);
        if (!path)
            continue;

        if (is_header_path(path))
        {
            add_pending_header_local(pending_headers, path, is_embedded);
        }
        else
        {
            add_pending_source(ctx, path, is_embedded);
        }
    }
}

/* Pop next pending source, or NULL if empty */
static CS_PendingDependency *pop_pending_source(CompilerContext *ctx)
{
    CS_PendingDependency *dep = ctx->pending_sources;
    if (dep)
    {
        ctx->pending_sources = dep->next;
        dep->next = NULL;
    }
    return dep;
}

static void append_stmt_list(StatementList **dst, StatementList *src)
{
    if (!src)
        return;
    if (!*dst)
    {
        *dst = src;
        return;
    }
    StatementList *last = *dst;
    while (last->next)
        last = last->next;
    last->next = src;
}

static void append_decl_list(DeclarationList **dst, DeclarationList *src)
{
    if (!src)
        return;
    if (!*dst)
    {
        *dst = src;
        return;
    }
    DeclarationList *last = *dst;
    while (last->next)
        last = last->next;
    last->next = src;
}

/* Parse a single .c file (no mean_check, just parse and collect dependencies) */
static bool compile_source_internal(CompilerContext *ctx, const char *compile_path, bool is_embedded)
{
    if (is_in_dependency_list(ctx->compiled_deps, compile_path, is_embedded))
        return true;

    /* Mark as compiled early to prevent re-entry during parsing */
    mark_as_compiled(ctx, compile_path, is_embedded);

    unsigned char *input_bytes = NULL;
    int input_size = 0;
    bool read_ok = false;
    bool input_owned = false;

    if (is_embedded)
    {
        const char *name = strrchr(compile_path, '/');
        name = name ? name + 1 : compile_path;
        const EmbeddedFile *embedded = embedded_find(name);
        if (embedded)
        {
            input_bytes = (unsigned char *)embedded->data;
            input_size = embedded->size;
            read_ok = true;
        }
    }
    else
    {
        read_ok = cs_read_file_bytes(compile_path, &input_bytes, &input_size);
        input_owned = read_ok;
    }
    if (!read_ok)
    {
        fprintf(stderr, "error: file not found: %s\n", compile_path);
        return false;
    }

    /* Create fresh TranslationUnit for this source file */
    TranslationUnit *tu = tu_create(ctx, compile_path);

    CS_ScannerConfig config = {
        .source_path = compile_path,
        .input_bytes = input_bytes,
        .input_size = input_size,
        .tu = tu,
    };

    Scanner *scanner = cs_create_scanner(&config);
    if (!scanner)
    {
        if (input_owned)
            free(input_bytes);
        tu_destroy(tu);
        return false;
    }

    /* Create FileDecl for this source (declarations will be added during parsing) */
    tu->current_file_decl = header_store_get_or_create(ctx->header_store, compile_path);

    /* Add to header_index (visible in this TU) */
    header_index_add_file(tu->header_index, tu->current_file_decl);

    if (yyparse(scanner))
    {
        DBG_PRINT("Parse Error");
        cs_delete_scanner(scanner);
        if (input_owned)
            free(input_bytes);
        exit(1);
    }

    /* Save source file's FileDecl */
    FileDecl *source_file_decl = tu->current_file_decl;

    /* Collect dependencies from scanner into local header queue */
    CS_PendingDependency *pending_headers = NULL;
    collect_dependencies_to_lists(ctx, scanner, &pending_headers);

    cs_delete_scanner(scanner);
    if (input_owned)
        free(input_bytes);

    /* Process header queue: parse each header, collect its deps, repeat */
    CS_PendingDependency *hdr;
    while ((hdr = pop_pending_header_local(&pending_headers)) != NULL)
    {
        const char *header_path = hdr->path;
        bool hdr_is_embedded = hdr->is_embedded;

        /* Parse header if not already in header_store */
        if (!header_store_is_parsed(ctx->header_store, header_path))
        {
            if (!parse_header_internal(ctx, header_path, hdr_is_embedded, &pending_headers))
            {
                free(hdr->path);
                free(hdr);
                free_dependency_list(pending_headers);
                return false;
            }
        }

        /* Add to this TU's header_index */
        FileDecl *fd = header_store_find(ctx->header_store, header_path);
        if (fd && !header_index_contains(tu->header_index, fd))
        {
            header_index_add_file(tu->header_index, fd);

            /* Also add stored dependencies of this header to pending queue */
            int dep_count = file_decl_dependency_count(fd);
            for (int di = 0; di < dep_count; di++)
            {
                FileDependency *dep = file_decl_get_dependency(fd, di);
                if (dep)
                {
                    add_pending_header_local(&pending_headers, dep->path, dep->is_embedded);
                }
            }
        }

        free(hdr->path);
        free(hdr);
    }

    /* Restore source file's FileDecl */
    tu->current_file_decl = source_file_decl;

    /* Note: Do NOT call store_function_prototypes here. Prototypes from included
     * headers are already stored in their respective header FileDeclss by
     * parse_header_internal. Storing them again here would incorrectly associate
     * them with the source file's class name instead of the header's class name. */

    /* Per-TU mean_check: only this .c and its included headers are visible.
     * Other .c files are NOT visible - enforces translation unit isolation. */
    if (!do_mean_check_for_tu(tu, source_file_decl))
    {
        return false;
    }

    /* Aggregate statements and declarations to ctx (for later codegen) */
    StatementList *tmp_stmts = ctx->all_statements;
    append_stmt_list(&tmp_stmts, tu->stmt_list);
    ctx->all_statements = tmp_stmts;

    DeclarationList *tmp_decls = ctx->all_declarations;
    append_decl_list(&tmp_decls, tu->decl_list);
    ctx->all_declarations = tmp_decls;

    return true;
}

bool CS_compile(CompilerContext *ctx, const char *path, bool is_embedded)
{
    if (!ctx || !path || !path[0])
        return false;

    /* Add initial entry to source queue */
    add_pending_source(ctx, path, is_embedded);

    /* Process source queue (headers are processed inside compile_source_internal).
     * Each source file has its own per-TU mean_check inside compile_source_internal. */
    CS_PendingDependency *dep;
    while ((dep = pop_pending_source(ctx)) != NULL)
    {
        bool ok = compile_source_internal(ctx, dep->path, dep->is_embedded);

        free(dep->path);
        free(dep);

        if (!ok)
            return false;
    }

    return true;
}

/* Compile a single source file for codegen phase (no mean_check) */
bool compile_source_for_codegen(CompilerContext *ctx, const char *path, bool is_embedded)
{
    if (!ctx || !path || !path[0])
        return false;

    return compile_source_internal(ctx, path, is_embedded);
}

/* Helper: Get corresponding .c path from .h path */
static char *get_corresponding_source(const char *header_path)
{
    int len = strlen(header_path);
    if (len < 3 || strcmp(header_path + len - 2, ".h") != 0)
        return NULL;

    char *source_path = (char *)calloc(len + 1, sizeof(char));
    strncpy(source_path, header_path, len - 1);
    source_path[len - 1] = 'c';
    source_path[len] = '\0';
    return source_path;
}

/* Parse a single header file. Dependencies are collected into pending_headers_out.
 * Each header is parsed with its own fresh TranslationUnit (no recursion). */
static bool parse_header_internal(CompilerContext *ctx, const char *header_path, bool is_embedded,
                                  CS_PendingDependency **pending_headers_out)
{
    if (!ctx || !header_path)
        return false;

    /* Check if already parsed (use HeaderStore) */
    if (header_store_is_parsed(ctx->header_store, header_path))
        return true;

    /* Mark as compiled early to prevent re-entry */
    mark_as_compiled(ctx, header_path, is_embedded);

    unsigned char *input_bytes = NULL;
    int input_size = 0;
    bool read_ok = false;
    bool input_owned = false;

    if (is_embedded)
    {
        const char *name = strrchr(header_path, '/');
        name = name ? name + 1 : header_path;
        const EmbeddedFile *embedded = embedded_find(name);
        if (embedded)
        {
            input_bytes = (unsigned char *)embedded->data;
            input_size = embedded->size;
            read_ok = true;
        }
    }
    else
    {
        read_ok = cs_read_file_bytes(header_path, &input_bytes, &input_size);
        input_owned = read_ok;
    }

    if (!read_ok)
    {
        DBG_PRINT("[header] cannot open %s\n", header_path);
        return false;
    }

    /* Create fresh TranslationUnit for this header */
    TranslationUnit *tu = tu_create(ctx, header_path);

    CS_ScannerConfig config = {
        .source_path = header_path,
        .input_bytes = input_bytes,
        .input_size = input_size,
        .tu = tu,
    };

    Scanner *scanner = cs_create_scanner(&config);
    if (!scanner)
    {
        if (input_owned)
            free(input_bytes);
        return false;
    }

    /* Create FileDecl for this header (declarations will be added during parsing) */
    tu->current_file_decl = header_store_get_or_create(ctx->header_store, header_path);

    if (yyparse(scanner) != 0)
    {
        cs_delete_scanner(scanner);
        if (input_owned)
            free(input_bytes);
        return false;
    }

    /* Collect dependencies into output list (no recursive parsing here) */
    collect_dependencies_to_lists(ctx, scanner, pending_headers_out);

    /* Store header dependencies in FileDecl for later reuse */
    FileDecl *fd = tu->current_file_decl;
    int dep_count = cs_scanner_dependency_count(scanner);
    for (int i = 0; i < dep_count; ++i)
    {
        const char *dep_path = cs_scanner_dependency_path(scanner, i);
        bool dep_is_embedded = cs_scanner_dependency_is_embedded(scanner, i);
        if (dep_path && is_header_path(dep_path))
        {
            file_decl_add_dependency(fd, dep_path, dep_is_embedded);
        }
    }

    cs_delete_scanner(scanner);
    if (input_owned)
        free(input_bytes);

    /* Set corresponding source for this header */
    char *corresponding_source = get_corresponding_source(header_path);
    HeaderDecl *hd = header_store_find(ctx->header_store, header_path);
    if (hd && corresponding_source)
    {
        hd->corresponding_source = strdup(corresponding_source);
    }

    /* Auto-add corresponding source to compile queue if it exists */
    if (corresponding_source)
    {
        if (is_embedded)
        {
            /* Check if embedded source exists */
            const char *name = strrchr(corresponding_source, '/');
            name = name ? name + 1 : corresponding_source;
            const EmbeddedFile *embedded_src = embedded_find(name);
            if (embedded_src)
            {
                add_pending_source(ctx, corresponding_source, true);
            }
        }
        else
        {
            /* Check if source file exists on disk */
            FILE *fp = fopen(corresponding_source, "rb");
            if (fp)
            {
                fclose(fp);
                add_pending_source(ctx, corresponding_source, false);
            }
        }
        free(corresponding_source);
    }

    return true;
}

void cs_add_runtime_dependency(CompilerContext *ctx, const char *header_name)
{
    if (!ctx || !header_name)
        return;

    /* Parse header and all its dependencies using the same loop pattern */
    CS_PendingDependency *pending_headers = NULL;
    add_pending_header_local(&pending_headers, header_name, true);

    CS_PendingDependency *hdr;
    while ((hdr = pop_pending_header_local(&pending_headers)) != NULL)
    {
        if (!header_store_is_parsed(ctx->header_store, hdr->path))
        {
            parse_header_internal(ctx, hdr->path, hdr->is_embedded, &pending_headers);
        }
        free(hdr->path);
        free(hdr);
    }
}
