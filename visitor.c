/* visitor.c - implementation file for visitor.h
 * Required by Cminor's header/implementation convention.
 * Contains minimal implementation for the Visitor base type.
 */

#include "visitor.h"

/* Delete visitor - simple free (Cminor: free is no-op, GC handles cleanup) */
void delete_visitor(Visitor *visitor)
{
    (void)visitor;
    /* No action needed - GC handles cleanup */
}
