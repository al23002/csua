#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codegenvisitor_stmt_basic.h"
#include "codegenvisitor.h"
#include "codegenvisitor_util.h"
#include "codegenvisitor_stmt_util.h"
#include "codebuilder_part2.h"
#include "codegen_symbols.h"
#include "codegen_constants.h"
#include "cminor_type.h"

void enter_generic_stmt(Statement *stmt, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    handle_if_boundary(cg, stmt);
    handle_for_body_entry(cg, stmt);
    handle_switch_entry(cg, stmt);
    /* No scope creation for generic statements (Javac-style block scoping) */
}

void leave_generic_stmt(Statement *stmt, Visitor *visitor)
{
    (void)stmt;
    (void)visitor;
    /* No scope cleanup for generic statements */
}

void enter_compound_stmt(Statement *stmt, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    handle_if_boundary(cg, stmt);
    handle_for_body_entry(cg, stmt);
    handle_switch_entry(cg, stmt);
    cg_begin_scope(cg, true);
}

void leave_compound_stmt(Statement *stmt, Visitor *visitor)
{
    (void)stmt;
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    cg_end_scope(cg, "compound statement");
}

void leave_exprstmt(Statement *stmt, Visitor *visitor)
{
    (void)stmt;
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    if (cg->builder->frame->stack_count > 0)
    {
        /* Use pop_value to correctly handle category 2 types (long, double) */
        codebuilder_build_pop_value(cg->builder);
    }
    /* No scope cleanup (block-level scoping) */
}
