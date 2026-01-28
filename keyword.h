#pragma once

struct OPE
{
    char *name;
    int type;
};

struct OPE *in_word_set(char *str, unsigned int len);
