#pragma once

#include <stddef.h>

#include "ast.h"
#include "parser.h"

/* Forward declarations for self-referential types */
typedef struct Macro Macro;
typedef struct MacroArgument MacroArgument;
typedef struct ConditionalFrame ConditionalFrame;

typedef struct
{
    int type;
    char *text;
    int int_value;
} PreprocessorToken;

/* Dependency entry: source file to compile (moved here before Preprocessor struct) */
typedef struct
{
    char *path;       /* File path (e.g., "foo.c") */
    bool is_embedded; /* True if this is an embedded file */
} PP_Dependency;

/* ByteBuffer - Source file content buffer */
typedef struct ByteBuffer
{
    const char *data;   /* Buffer data (embedded or malloc'd) */
    int size;           /* Buffer size */
    int position;       /* Current position */
    bool owned;         /* Whether to free data */
    int line;           /* Current line number */
    int line_start_pos; /* Line start position (for column calc) */
} ByteBuffer;

/* SourceFrame - Single source file on stack */
typedef struct
{
    ByteBuffer *buffer;
    char *path;
    char *dir;
    char *logical_path;
    int logical_line;
} SourceFrame;

/* SourceStack - Stack of source files (for includes) */
typedef struct
{
    SourceFrame *frames;
    int size;
    int capacity;
} SourceStack;

/* MacroExpansion - State for expanding a single macro */
typedef struct
{
    const char *text;
    int position;
    Macro *macro;
} MacroExpansion;

/* MacroExpansionStack - Stack of macro expansions */
typedef struct
{
    MacroExpansion *data;
    int size;
    int capacity;
} MacroExpansionStack;

/* MacroArgument - Argument for function-like macro */
struct MacroArgument
{
    char *text;
    PreprocessorToken *tokens;
    int token_count;
    MacroArgument *next;
};

/* Macro - Preprocessor macro definition */
struct Macro
{
    char *name;
    bool is_function;
    bool is_variadic;
    int param_count;
    char **params;
    char *body;
    bool expanding;
    bool builtin_file;
    bool builtin_line;
    Macro *next;
};

/* ConditionalFrame - State for #if/#ifdef/#else/#endif */
struct ConditionalFrame
{
    bool active;
    bool seen_true_branch;
    bool in_else;
    ConditionalFrame *next;
};

/* Preprocessor - Main preprocessor state */
typedef struct Preprocessor
{
    SourceStack *sources;
    Scanner *scanner;
    Macro *macros;
    MacroExpansionStack *expansions;
    ConditionalFrame *conditionals;
    bool at_line_start;
    char **include_dirs;
    int include_dir_count;
    int include_dir_capacity;
    char *initial_source_path;
    ByteBuffer *initial_buffer;
    const char *token_path;
    int token_line;
    char **retained_strings;
    int retained_string_count;
    int retained_string_capacity;
    /* Dependency tracking */
    PP_Dependency *dependencies;
    int dependency_count;
    int dependency_capacity;
} Preprocessor;

Preprocessor *pp_create(Scanner *scanner);
void pp_destroy(Preprocessor *pp);

void pp_set_initial_source(Preprocessor *pp, const char *path, ByteBuffer *buffer);
void pp_add_include_dir(Preprocessor *pp, const char *path);
int pp_next_token(Preprocessor *pp, YYSTYPE *yylval);

/* Dependency tracking API */
void pp_add_dependency(Preprocessor *pp, const char *path, bool is_embedded);
int pp_get_dependency_count(Preprocessor *pp);
const PP_Dependency *pp_get_dependency(Preprocessor *pp, int index);

/* ByteBuffer helper functions */
ByteBuffer *load_from_bytes(const unsigned char *data, int size);
const char *pp_current_text(Preprocessor *pp);
int pp_current_line(Preprocessor *pp);
void pp_get_token_location(Preprocessor *pp, const char **path, int *line);
