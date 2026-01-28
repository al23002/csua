#pragma once

/*
 * scanner.h - Scanner definitions and API
 *
 * Contains:
 * - Scanner struct definition
 * - Scanner creation and management functions
 */

#include <stdio.h>

#include "cminor_base.h"
#include "compiler.h"
#include "preprocessor.h"

struct Scanner_tag
{
    struct Preprocessor *preprocessor;
    TranslationUnit *tu;
    CS_Creator *creator; /* Creator context (heap allocated) */
    char *yytext;
    int ytp;
    int yt_max;
    char *initial_source_path;
    struct ByteBuffer *initial_buffer;
};

/* Scanner lifecycle */
Scanner *cs_create_scanner(const CS_ScannerConfig *config);
void cs_delete_scanner(Scanner *scanner);

/* Scanner configuration */
void cs_add_include_dir(Scanner *scanner, const char *path);

/* Scanner state access */
int get_current_line(Scanner *scanner);
const char *cs_scanner_text(Scanner *scanner);
TranslationUnit *cs_scanner_get_tu(Scanner *scanner);
CS_Creator *cs_scanner_get_creator(Scanner *scanner);

/* Dependency tracking */
int cs_scanner_dependency_count(Scanner *scanner);
const char *cs_scanner_dependency_path(Scanner *scanner, int index);
bool cs_scanner_dependency_is_embedded(Scanner *scanner, int index);

/* Parser location type - used by bison for error reporting */
typedef struct YYLTYPE
{
    const char *filename;
    int first_line;
    int first_column;
    int last_line;
    int last_column;
} YYLTYPE;

/* Lexer function */
int yylex(YYSTYPE *yylval_param, YYLTYPE *yylloc_param, Scanner *scanner);
