#pragma once

#include "codegenvisitor.h"

typedef enum
{
    CG_SYMBOL_STATIC,
    CG_SYMBOL_LOCAL,
    CG_SYMBOL_PARAM,
} CodegenSymbolKind;

typedef struct CodegenSymbol_tag
{
    Declaration *decl;
    CodegenSymbolKind kind;
    int index;
    struct CodegenSymbol_tag *next;
} CodegenSymbol;

typedef struct CodegenSymbolInfo_tag
{
    CodegenSymbolKind kind;
    int index;
} CodegenSymbolInfo;

CodegenSymbolInfo cg_ensure_symbol(CodegenVisitor *v, Declaration *decl);
void cg_begin_scope(CodegenVisitor *v, bool track_symbols);
void cg_end_scope(CodegenVisitor *v, const char *context);
void cg_clear_symbols(CodegenVisitor *v);
