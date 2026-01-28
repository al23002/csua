#pragma once

/*
 * util.h - Utility functions for Cminor compiler
 *
 * Contains:
 * - Search functions (declarations, functions)
 * - Count functions (parameters, arguments)
 * - File I/O utilities
 */

#include "cminor_base.h"

/* Search functions */
Declaration *cs_search_decl_in_block();
Declaration *cs_search_decl_global(CS_Compiler *compiler, const char *name);
FunctionDeclaration *cs_search_function(CS_Compiler *compiler, const char *name);

/* Count functions */
int cs_count_parameters(ParameterList *param);
int cs_count_arguments(ArgumentList *arg);

/* Path utilities */
char *cs_class_name_from_path(const char *path);

/* File I/O */
bool cs_read_file_bytes(const char *path, unsigned char **out_data, int *out_size);
