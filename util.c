
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "compiler.h"
#include "util.h"
#include "header_store.h"
#include "header_index.h"

bool cs_read_file_bytes(const char *path, unsigned char **out_data, int *out_size)
{
    if (!path || !out_data || !out_size)
        return false;

    FILE *fp = fopen(path, "rb");
    if (!fp)
        return false;

    int capacity = 4096;
    int size = 0;
    unsigned char *data = (unsigned char *)calloc(capacity, sizeof(unsigned char));
    if (!data)
    {
        fclose(fp);
        return false;
    }

    while (1)
    {
        int n = fread(data + size, 1, capacity - size, fp);
        if (n <= 0)
            break;
        size += n;
        if (size == capacity)
        {
            int new_capacity = capacity * 2;
            unsigned char *new_data = (unsigned char *)calloc(new_capacity, sizeof(unsigned char));
            if (!new_data)
            {
                fclose(fp);
                return false;
            }
            memcpy(new_data, data, size);
            data = new_data;
            capacity = new_capacity;
        }
    }
    fclose(fp);

    *out_data = data;
    *out_size = size;
    return true;
}

static Declaration *search_decls_from_list(DeclarationList *list,
                                           const char *name)
{
    for (; list; list = list->next)
    {
        if (!strcmp(list->decl->name, name))
        {
            return list->decl;
        }
    }
    return NULL;
}
// search from a block temporary
Declaration *cs_search_decl_in_block() { return NULL; }

Declaration *cs_search_decl_global(CS_Compiler *compiler, const char *name)
{
    if (!compiler)
        return NULL;

    /* First search in current TU's declarations */
    Declaration *decl = search_decls_from_list(compiler->decl_list, name);
    if (decl)
        return decl;

    /* Then search in header_index (extern declarations from included headers) */
    if (compiler->header_index)
    {
        decl = header_index_find_declaration(compiler->header_index, name);
        if (decl)
            return decl;
    }

    return NULL;
}

static FunctionDeclaration *search_function_from_list(
    FunctionDeclarationList *list, const char *name)
{
    for (; list; list = list->next)
    {
        if (!strcmp(list->func->name, name))
        {
            return list->func;
        }
    }
    return NULL;
}

FunctionDeclaration *cs_search_function(CS_Compiler *compiler, const char *name)
{
    if (!compiler)
        return NULL;

    /* Search in current_file_decl->functions (has resolved types from mean_check) */
    if (compiler->current_file_decl)
    {
        FunctionDeclaration *func = search_function_from_list(
            compiler->current_file_decl->functions, name);
        if (func)
            return func;
    }

    /* Search visible FileDecls in header_index (also has resolved types from mean_check) */
    if (compiler->header_index)
    {
        FunctionDeclaration *func = header_index_find_function(compiler->header_index, name);
        if (func)
            return func;
    }

    return NULL;
}

int cs_count_parameters(ParameterList *param)
{
    uint8_t count = 0;
    for (; param && !param->is_ellipsis; param = param->next)
    {
        /* Skip void parameter (type==NULL && name==NULL && !is_ellipsis) */
        if (param->type == NULL && param->parsed_type == NULL && param->name == NULL)
        {
            continue;
        }
        ++count;
    }
    return count;
}

int cs_count_arguments(ArgumentList *arg)
{
    uint8_t count = 0;
    for (; arg; arg = arg->next)
    {
        ++count;
    }
    return count;
}

char *cs_class_name_from_path(const char *path)
{
    const char *filename = path ? path : "Main";
    const char *slash = strrchr(filename, '/');
    const char *base_name = (slash && slash[1]) ? slash + 1 : filename;
    const char *dot = strrchr(base_name, '.');
    int base_len = dot ? (int)(dot - base_name) : strlen(base_name);

    if (base_len == 0)
    {
        base_name = "Main";
        base_len = strlen(base_name);
    }

    char *class_name = (char *)calloc(base_len + 1, sizeof(char));
    strncpy(class_name, base_name, base_len);
    class_name[base_len] = '\0';

    return class_name;
}
