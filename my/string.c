#include "string.h"

#include <stdlib.h>

int strlen(const char *s)
{
    int len = 0;
    while (s[len] != '\0')
        len = len + 1;
    return len;
}

char *strcpy(char *dest, const char *src)
{
    int i = 0;
    while (src[i] != '\0')
    {
        dest[i] = src[i];
        i = i + 1;
    }
    dest[i] = '\0';
    return dest;
}

int strcmp(const char *s1, const char *s2)
{
    int i = 0;
    while (s1[i] != '\0' && s2[i] != '\0')
    {
        if (s1[i] != s2[i])
            return s1[i] - s2[i];
        i = i + 1;
    }
    return s1[i] - s2[i];
}

int strncmp(const char *s1, const char *s2, int n)
{
    int i = 0;
    while (i < n && s1[i] != '\0' && s2[i] != '\0')
    {
        if (s1[i] != s2[i])
            return s1[i] - s2[i];
        i = i + 1;
    }
    if (i == n)
        return 0;
    return s1[i] - s2[i];
}

char *strchr(const char *s, int c)
{
    int i = 0;
    while (s[i] != '\0')
    {
        if (s[i] == c)
            return s + i;
        i = i + 1;
    }
    if (c == '\0')
        return s + i;
    return NULL;
}

char *strrchr(const char *s, int c)
{
    int last_idx = -1;
    int i = 0;
    while (s[i] != '\0')
    {
        if (s[i] == c)
            last_idx = i;
        i = i + 1;
    }
    if (c == '\0')
        return s + i;
    if (last_idx == -1)
        return NULL;
    return s + last_idx;
}

char *memcpy(char *dest, const char *src, int n)
{
    int i = 0;
    while (i < n)
    {
        dest[i] = src[i];
        i = i + 1;
    }
    return dest;
}

char *strdup(const char *s)
{
    int len = strlen(s);
    char *dup = (char *)calloc(len + 1, sizeof(char));
    strcpy(dup, s);
    return dup;
}

char *strncpy(char *dest, const char *src, int n)
{
    int i = 0;
    while (i < n && src[i] != '\0')
    {
        dest[i] = src[i];
        i = i + 1;
    }
    while (i < n)
    {
        dest[i] = '\0';
        i = i + 1;
    }
    return dest;
}
