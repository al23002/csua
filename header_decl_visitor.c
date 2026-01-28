#include <stdlib.h>

#include "header_decl_visitor.h"
#include "ast.h"
#include "compiler.h"
#include "header_store.h"
#include "parsed_type.h"

void header_decl_visitor_process(CS_Compiler *compiler, const char *source_path)
{
    (void)source_path; /* No longer needed - functions stored in FileDecl */

    if (!compiler || !compiler->current_file_decl)
        return;

    HeaderStore *store = compiler->header_store;
    if (!store)
        return;

    /* Iterate FileDecl->functions directly (prototypes added during parsing) */
    FunctionDeclarationList *cur = compiler->current_file_decl->functions;

    while (cur)
    {
        FunctionDeclaration *func = cur->func;

        if (func && func->body == NULL)
        {
            /* Resolve return type if not yet resolved */
            TypeSpecifier *return_type = func->type;
            if (!return_type && func->parsed_type)
            {
                return_type = cs_resolve_type(func->parsed_type, store, compiler);
                func->type = return_type;
            }

            /* Resolve parameter types */
            for (ParameterList *p = func->param; p; p = p->next)
            {
                if (p->is_ellipsis)
                    break;
                if (!p->type && p->parsed_type)
                {
                    p->type = cs_resolve_type(p->parsed_type, store, compiler);
                }
            }

            /* Function is already stored in FileDecl->functions,
             * no need for separate DeclarationRegistry */
        }

        cur = cur->next;
    }
}
