#pragma once

typedef void **va_list;

void va_start(va_list ap);
void *__builtin_va_arg(va_list ap);
void va_end(va_list ap);
