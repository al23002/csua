#pragma once

#include <stddef.h>

void *malloc(int size);
void *realloc(void *ptr, int size);
void *calloc(int nmemb, int size);

void free(void *ptr);
void exit(int status);
long strtol(const char *str, char **endptr, int base);
double strtod(const char *str, char **endptr);
float strtof(const char *str, char **endptr);
