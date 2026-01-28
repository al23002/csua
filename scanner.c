#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler.h"
#include "scanner.h"
#include "preprocessor.h"

static char *dup_string(const char *src)
{
    if (!src)
        return NULL;
    return strdup(src);
}

static void configure_initial_source(Scanner *scanner, const CS_ScannerConfig *config)
{
    const char *path = "stdin";
    ByteBuffer *buffer = NULL;

    if (config)
    {
        if (config->source_path)
        {
            path = config->source_path;
        }
        if (config->input_bytes)
        {
            buffer = load_from_bytes(config->input_bytes, config->input_size);
        }
    }

    free(scanner->initial_source_path);
    scanner->initial_source_path = dup_string(path);
    scanner->initial_buffer = buffer;
}

Scanner *cs_create_scanner(const CS_ScannerConfig *config)
{
    Scanner *scanner = (Scanner *)calloc(1, sizeof(Scanner));
    scanner->tu = config ? config->tu : NULL;
    /* Allocate creator context */
    scanner->creator = (CS_Creator *)calloc(1, sizeof(CS_Creator));
    scanner->creator->line_number = 1;
    scanner->creator->source_path = NULL;
    scanner->creator->tu = scanner->tu;
    configure_initial_source(scanner, config);
    scanner->preprocessor = pp_create(scanner);
    pp_set_initial_source(scanner->preprocessor, scanner->initial_source_path,
                          scanner->initial_buffer);
    scanner->initial_buffer = NULL; /* 所有権をppに移譲 */
    return scanner;
}

void cs_delete_scanner(Scanner *scanner)
{
    if (!scanner)
        return;
    pp_destroy(scanner->preprocessor);
    free(scanner->yytext);
    free(scanner->initial_source_path);
    free(scanner);
}

void cs_add_include_dir(Scanner *scanner, const char *path)
{
    if (!scanner)
        return;
    pp_add_include_dir(scanner->preprocessor, path);
}

int get_current_line(Scanner *scanner)
{
    if (!scanner)
        return 0;
    return pp_current_line(scanner->preprocessor);
}

const char *cs_scanner_text(Scanner *scanner)
{
    if (!scanner)
        return NULL;
    return pp_current_text(scanner->preprocessor);
}

int yylex(YYSTYPE *yylval_param, YYLTYPE *yylloc_param, Scanner *scanner)
{
    if (!scanner)
        return 0;
    int token = pp_next_token(scanner->preprocessor, yylval_param);
    const char *path = NULL;
    int line = 0;
    pp_get_token_location(scanner->preprocessor, &path, &line);
    if (yylloc_param)
    {
        yylloc_param->filename = path;
        yylloc_param->first_line = line;
        yylloc_param->last_line = line;
        yylloc_param->first_column = 0;
        yylloc_param->last_column = 0;
    }
    /* Update creator context with current location */
    scanner->creator->line_number = line;
    scanner->creator->source_path = path;
    return token;
}

int cs_scanner_dependency_count(Scanner *scanner)
{
    if (!scanner || !scanner->preprocessor)
        return 0;
    return pp_get_dependency_count(scanner->preprocessor);
}

const char *cs_scanner_dependency_path(Scanner *scanner, int index)
{
    if (!scanner || !scanner->preprocessor)
        return NULL;
    const PP_Dependency *dep = pp_get_dependency(scanner->preprocessor, index);
    return dep ? dep->path : NULL;
}

bool cs_scanner_dependency_is_embedded(Scanner *scanner, int index)
{
    if (!scanner || !scanner->preprocessor)
        return false;
    const PP_Dependency *dep = pp_get_dependency(scanner->preprocessor, index);
    return dep ? dep->is_embedded : false;
}

TranslationUnit *cs_scanner_get_tu(Scanner *scanner)
{
    if (!scanner)
        return NULL;
    return scanner->tu;
}

CS_Creator *cs_scanner_get_creator(Scanner *scanner)
{
    if (!scanner)
        return NULL;
    return scanner->creator;
}
