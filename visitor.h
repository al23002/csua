#pragma once

#include <stdlib.h>

/* Base visitor structure - empty after switch-based traversal migration.
 * Kept as common base type for MeanVisitor and CodegenVisitor. */
typedef struct Visitor_tag
{
    /* No fields - switch-based traversal replaced function pointer dispatch */
} Visitor;

/* Delete visitor - simple free */
void delete_visitor(Visitor *visitor);
