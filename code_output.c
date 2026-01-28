#include <stdlib.h>

#include "code_output.h"

CodeOutput *code_output_create()
{
    CodeOutput *out = (CodeOutput *)calloc(1, sizeof(CodeOutput));
    if (!out)
    {
        return NULL;
    }

    out->method = method_code_create();
    out->cp = cp_builder_create();
    out->owns_cp = true;

    if (!out->method || !out->cp)
    {
        method_code_destroy(out->method);
        cp_builder_destroy(out->cp);
        free(out);
        return NULL;
    }

    return out;
}

CodeOutput *code_output_create_with_cp(ConstantPoolBuilder *cp)
{
    if (!cp)
    {
        return NULL;
    }

    CodeOutput *out = (CodeOutput *)calloc(1, sizeof(CodeOutput));
    if (!out)
    {
        return NULL;
    }

    out->method = method_code_create();
    out->cp = cp;
    out->owns_cp = false; /* borrowed CP */

    if (!out->method)
    {
        free(out);
        return NULL;
    }

    return out;
}

void code_output_destroy(CodeOutput *out)
{
    if (!out)
    {
        return;
    }

    method_code_destroy(out->method);
    if (out->owns_cp)
    {
        cp_builder_destroy(out->cp);
    }
    free(out);
}

bool code_output_owns_cp(CodeOutput *out)
{
    return out ? out->owns_cp : false;
}

ConstantPoolBuilder *code_output_take_cp(CodeOutput *out)
{
    if (!out || !out->owns_cp)
    {
        return NULL;
    }
    ConstantPoolBuilder *cp = out->cp;
    out->owns_cp = false;
    return cp;
}

ConstantPoolBuilder *code_output_cp(CodeOutput *out)
{
    return out ? out->cp : NULL;
}

MethodCode *code_output_method(CodeOutput *out)
{
    return out ? out->method : NULL;
}

void code_output_reset_method(CodeOutput *out)
{
    if (!out)
    {
        return;
    }

    method_code_reset(out->method);
}
