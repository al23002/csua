#include <stdio.h>
#include <stdlib.h>

#include "classfile.h"
#include "ast.h"
#include "compiler.h"
#include "codebuilder_frame.h"
#include "codebuilder_types.h"
#include "codegenvisitor.h"
#include "codegen_symbols.h"
#include "codegen_jvm_types.h"

static bool is_global_declaration(CodegenVisitor *v, Declaration *decl)
{
    for (DeclarationList *d = v->compiler->decl_list; d; d = d->next)
    {
        if (d->decl == decl)
        {
            return true;
        }
    }
    return false;
}

static CodegenSymbol *lookup_symbol(CodegenVisitor *v, Declaration *decl)
{
    for (CodegenSymbol *sym = v->ctx.symbol_stack; sym;
         sym = sym->next)
    {
        if (sym->decl == decl)
        {
            return sym;
        }
    }
    return NULL;
}

static void pop_symbols_to(CodegenVisitor *v, CodegenSymbol *target)
{
    while (v->ctx.symbol_stack != target)
    {
        CodegenSymbol *sym = v->ctx.symbol_stack;
        v->ctx.symbol_stack = sym->next;
        free(sym);
    }
}

static CodegenSymbol *push_symbol(CodegenVisitor *v, Declaration *decl,
                                  CodegenSymbolKind kind, int index)
{
    CodegenSymbol *sym = (CodegenSymbol *)calloc(1, sizeof(CodegenSymbol));
    sym->decl = decl;
    sym->kind = kind;
    sym->index = index;
    sym->next = v->ctx.symbol_stack;
    v->ctx.symbol_stack = sym;
    return sym;
}

/* Local allocation is now delegated to CodeBuilder */

static CodegenSymbol *ensure_symbol_internal(CodegenVisitor *v, Declaration *decl)
{
    CodegenSymbol *sym = lookup_symbol(v, decl);
    if (sym)
    {
        return sym;
    }

    /* extern variables must use getstatic (not aload) */
    if (decl->is_extern)
    {
        return push_symbol(v, decl, CG_SYMBOL_STATIC, decl->index);
    }

    if (is_global_declaration(v, decl))
    {
        return push_symbol(v, decl, CG_SYMBOL_STATIC, decl->index);
    }

    /* Parameters have pre-assigned indices */
    if (decl->index >= 0)
    {
        int idx = decl->index;
        /* Ensure CodeBuilder knows about this slot.
         * For heap-lifted parameters, the slot contains an array (the box array),
         * not the original parameter type. Array type depends on the parameter type.
         *
         * NOTE: For heap-lifted parameters, use codebuilder_set_local instead of
         * codebuilder_set_param. The heap-lifted slot is NOT part of the JVM's
         * initial frame (determined by method descriptor). Using set_param would
         * incorrectly add it to initial_frame, causing StackMapTable errors. */
        CB_VerificationType param_type;
        if (decl->needs_heap_lift)
        {
            const char *array_desc = cg_heap_lift_array_descriptor(decl->type);
            param_type = cb_type_object(array_desc);
            codebuilder_set_local(v->builder, idx, param_type);
        }
        else
        {
            param_type = cb_type_from_c_type(decl->type);
            codebuilder_set_param(v->builder, idx, param_type);
        }
        return push_symbol(v, decl, CG_SYMBOL_PARAM, idx);
    }

    /* Allocate new local slot via CodeBuilder (Javac-style).
     * For heap-lifted locals, the slot contains an array (the box),
     * not the original type. */
    CB_VerificationType local_type;
    if (decl->needs_heap_lift)
    {
        const char *array_desc = cg_heap_lift_array_descriptor(decl->type);
        local_type = cb_type_object(array_desc);
    }
    else
    {
        local_type = cb_type_from_c_type(decl->type);
    }
    int local_idx = codebuilder_allocate_local(v->builder, local_type);

    return push_symbol(v, decl, CG_SYMBOL_LOCAL, local_idx);
}

CodegenSymbolInfo cg_ensure_symbol(CodegenVisitor *v, Declaration *decl)
{
    CodegenSymbol *sym = ensure_symbol_internal(v, decl);
    CodegenSymbolInfo info = {sym->kind, sym->index};
    return info;
}

void cg_clear_symbols(CodegenVisitor *v)
{
    pop_symbols_to(v, NULL);
    v->ctx.symbol_stack = NULL;
}

void cg_begin_scope(CodegenVisitor *v, bool track_symbols)
{
    (void)track_symbols; /* No longer used - symbols persist for entire function */

    /* Delegate block management to CodeBuilder (Javac-style).
     * CodeBuilder tracks locals_count for slot reuse at block exit.
     * Symbol mappings (Declaration -> slot) persist for entire function.
     * Depth limit is enforced by CodeBuilder (CB_MAX_SCOPE_DEPTH). */
    codebuilder_begin_block(v->builder);

    ++v->ctx.scope_depth;
}

void cg_end_scope(CodegenVisitor *v, const char *context)
{
    if (v->ctx.scope_depth == 0)
    {
        fprintf(stderr, "scope stack underflow in %s\n", context ? context : "stmt");
        return;
    }

    --v->ctx.scope_depth;

    /* Delegate block exit to CodeBuilder - handles locals slot reuse.
     * Symbol mappings (Declaration -> slot) persist for the entire function
     * (javac-style). The same Declaration always maps to the same slot. */
    codebuilder_end_block(v->builder);
}
