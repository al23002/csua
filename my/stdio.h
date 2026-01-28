#pragma once
#include <stdarg.h>

typedef struct
{
    void *stream;
} FILE;

/* Standard streams - initialized by clinit */
extern FILE *stdout;
extern FILE *stderr;

int printf(const char *fmt, ...);
int fprintf(FILE *file, const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int vfprintf(FILE *file, const char *fmt, va_list ap);
int snprintf(char *str, int size, const char *fmt, ...);

int fflush(FILE *file);
int vsnprintf(char *str, int size, const char *fmt, va_list ap);
int sscanf(const char *str, const char *fmt, ...);

FILE *fopen(const char *filename, const char *mode);
int fwrite(const char *ptr, int size, int count, FILE *file);
int fread(char *ptr, int size, int count, FILE *file);
int fclose(FILE *file);

enum
{
    EOF = -1
};
