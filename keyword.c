#include <string.h>

#include "keyword.h"
#include "ast.h"
#include "parser.h"

static const struct OPE keyword_list[] = {
    {"NULL", NULL_T},
    {"bool", BOOL_T},
    {"break", BREAK},
    {"case", CASE},
    {"char", CHAR_T},
    {"const", CONST_T},
    {"continue", CONTINUE},
    {"default", DEFAULT},
    {"do", DO},
    {"double", DOUBLE_T},
    {"else", ELSE},
    {"enum", ENUM_T},
    {"extern", EXTERN_T},
    {"false", FALSE_T},
    {"float", FLOAT_T},
    {"for", FOR},
    {"goto", GOTO},
    {"if", IF},
    {"int", INT_T},
    {"long", LONG_T},
    {"return", RETURN},
    {"short", SHORT_T},
    {"sizeof", SIZEOF},
    {"static", STATIC_T},
    {"struct", STRUCT_T},
    {"switch", SWITCH},
    {"true", TRUE_T},
    {"typedef", TYPEDEF_T},
    {"union", UNION_T},
    {"unsigned", UNSIGNED_T},
    {"void", VOID_T},
    {"while", WHILE},
};

struct OPE *in_word_set(char *str, unsigned int len)
{
    (void)len;
    for (int i = 0; i < sizeof keyword_list / sizeof *keyword_list; ++i)
    {
        if (strcmp(str, keyword_list[i].name) == 0)
        {
            return (struct OPE *)(&keyword_list[i]);
        }
    }
    return NULL;
}
