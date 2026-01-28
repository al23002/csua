#include <stdlib.h>
#include <string.h>

#include "constant_pool.h"

ConstantPoolBuilder *cp_builder_create()
{
    ConstantPoolBuilder *cp = (ConstantPoolBuilder *)calloc(1, sizeof(ConstantPoolBuilder));
    if (!cp)
    {
        return NULL;
    }
    cp->cf_cp = cf_cp_create();
    return cp;
}

void cp_builder_destroy(ConstantPoolBuilder *cp)
{
    if (!cp)
    {
        return;
    }

    if (cp->cf_cp)
    {
        cf_cp_free(cp->cf_cp);
    }
    if (cp->metadata)
    {
        free(cp->metadata);
    }
    free(cp);
}

/* Get the underlying CF_ConstantPool */
CF_ConstantPool *cp_builder_get_cf_cp(ConstantPoolBuilder *cp)
{
    return cp ? cp->cf_cp : NULL;
}

/* Take ownership of the underlying CF_ConstantPool (sets internal pointer to NULL) */
CF_ConstantPool *cp_builder_take_cf_cp(ConstantPoolBuilder *cp)
{
    if (!cp)
    {
        return NULL;
    }
    CF_ConstantPool *result = cp->cf_cp;
    cp->cf_cp = NULL;
    return result;
}

/* Metadata accessors - for backwards compatibility */
CP_Constant *cp_builder_entries(ConstantPoolBuilder *cp)
{
    return cp ? cp->metadata : NULL;
}

int cp_builder_count(ConstantPoolBuilder *cp)
{
    return cp ? cp->metadata_count : 0;
}

static void ensure_metadata_capacity(ConstantPoolBuilder *cp, int index)
{
    if (index < cp->metadata_capacity)
    {
        return;
    }

    int new_cap = cp->metadata_capacity ? cp->metadata_capacity * 2 : 16;
    while (new_cap <= index)
    {
        new_cap *= 2;
    }

    CP_Constant *new_metadata = (CP_Constant *)calloc(new_cap, sizeof(CP_Constant));
    if (cp->metadata)
    {
        for (int i = 0; i < cp->metadata_count; i++)
        {
            new_metadata[i] = cp->metadata[i];
        }
        free(cp->metadata);
    }
    cp->metadata = new_metadata;
    cp->metadata_capacity = new_cap;
}

static void set_metadata_at(ConstantPoolBuilder *cp, int index, CP_ConstantType type)
{
    ensure_metadata_capacity(cp, index);
    cp->metadata[index].type = type;
    if (index >= cp->metadata_count)
    {
        cp->metadata_count = index + 1;
    }
}

int cp_builder_add_int(ConstantPoolBuilder *cp, int value)
{
    if (!cp)
    {
        return 0;
    }
    int idx = cf_cp_add_integer(cp->cf_cp, value);
    set_metadata_at(cp, idx, CP_CONST_INT);
    cp->metadata[idx].u.c_int = value;
    return idx;
}

int cp_builder_add_long(ConstantPoolBuilder *cp, long value)
{
    if (!cp)
    {
        return 0;
    }
    int idx = cf_cp_add_long(cp->cf_cp, value);
    set_metadata_at(cp, idx, CP_CONST_LONG);
    cp->metadata[idx].u.c_long = value;
    return idx;
}

int cp_builder_add_float(ConstantPoolBuilder *cp, float value)
{
    if (!cp)
    {
        return 0;
    }
    int idx = cf_cp_add_float(cp->cf_cp, value);
    set_metadata_at(cp, idx, CP_CONST_FLOAT);
    cp->metadata[idx].u.c_float = value;
    return idx;
}

int cp_builder_add_double(ConstantPoolBuilder *cp, double value)
{
    if (!cp)
    {
        return 0;
    }
    int idx = cf_cp_add_double(cp->cf_cp, value);
    set_metadata_at(cp, idx, CP_CONST_DOUBLE);
    cp->metadata[idx].u.c_double = value;
    return idx;
}

int cp_builder_add_string(ConstantPoolBuilder *cp, const char *value)
{
    if (!cp || !value)
    {
        return 0;
    }
    int len = strlen(value);
    return cp_builder_add_string_len(cp, value, len);
}

int cp_builder_add_string_len(ConstantPoolBuilder *cp, const char *data, int len)
{
    if (!cp || !data)
    {
        return 0;
    }
    int idx = cf_cp_add_string_len(cp->cf_cp, (const uint8_t *)data, len);
    set_metadata_at(cp, idx, CP_CONST_STRING);
    cp->metadata[idx].u.c_string.len = len;
    cp->metadata[idx].u.c_string.data = (uint8_t *)calloc(len, sizeof(uint8_t));
    memcpy(cp->metadata[idx].u.c_string.data, data, len);
    return idx;
}

int cp_builder_increment_count(ConstantPoolBuilder *cp)
{
    if (!cp)
    {
        return 0;
    }
    /* For backwards compatibility - allocate a slot in cf_cp */
    /* This is used by codegen_constants.c which directly manipulates entries */
    int idx = cf_cp_add_integer(cp->cf_cp, 0); /* placeholder */
    set_metadata_at(cp, idx, 0);
    return idx;
}

void cp_builder_ensure_capacity(ConstantPoolBuilder *cp, int additional)
{
    /* For backwards compatibility */
    if (!cp)
    {
        return;
    }
    ensure_metadata_capacity(cp, cp->metadata_count + additional);
}

int cp_builder_add_methodref(ConstantPoolBuilder *cp, const char *class_name,
                             const char *method_name, const char *descriptor)
{
    if (!cp)
    {
        return 0;
    }

    int idx = cf_cp_add_methodref(cp->cf_cp, class_name, method_name, descriptor);
    set_metadata_at(cp, idx, CP_CONST_METHOD);
    cp->metadata[idx].u.c_method.class_name = strdup(class_name ? class_name : "");
    cp->metadata[idx].u.c_method.name = strdup(method_name ? method_name : "");
    cp->metadata[idx].u.c_method.descriptor = strdup(descriptor ? descriptor : "");
    cp->metadata[idx].u.c_method.is_external = true;
    cp->metadata[idx].u.c_method.is_native = false;
    cp->metadata[idx].u.c_method.arg_count = 0;
    cp->metadata[idx].u.c_method.max_stack = 0;
    cp->metadata[idx].u.c_method.max_locals = 0;
    cp->metadata[idx].u.c_method.func_decl = NULL;
    return idx;
}

int cp_builder_add_methodref_typed(ConstantPoolBuilder *cp, const char *class_name,
                                   const char *method_name, const char *descriptor,
                                   FunctionDeclaration *func, int arg_count)
{
    int idx = cp_builder_add_methodref(cp, class_name, method_name, descriptor);
    if (!cp || idx <= 0)
    {
        return idx;
    }
    cp->metadata[idx].u.c_method.func_decl = func;
    cp->metadata[idx].u.c_method.arg_count = arg_count;
    cp->metadata[idx].u.c_method.is_external = false;
    return idx;
}

int cp_builder_add_fieldref(ConstantPoolBuilder *cp, const char *class_name,
                            const char *field_name, const char *descriptor)
{
    if (!cp)
    {
        return 0;
    }

    int idx = cf_cp_add_fieldref(cp->cf_cp, class_name, field_name, descriptor);
    set_metadata_at(cp, idx, CP_CONST_FIELD);
    cp->metadata[idx].u.c_field.class_name = strdup(class_name ? class_name : "");
    cp->metadata[idx].u.c_field.name = strdup(field_name ? field_name : "");
    cp->metadata[idx].u.c_field.descriptor = strdup(descriptor ? descriptor : "");
    cp->metadata[idx].u.c_field.is_external = true;
    cp->metadata[idx].u.c_field.field_index = 0;
    return idx;
}

int cp_builder_add_class(ConstantPoolBuilder *cp, const char *class_name)
{
    if (!cp)
    {
        return 0;
    }

    int idx = cf_cp_add_class(cp->cf_cp, class_name);
    set_metadata_at(cp, idx, CP_CONST_CLASS);
    cp->metadata[idx].u.c_class.name = strdup(class_name ? class_name : "");
    cp->metadata[idx].u.c_class.class_index = -1;
    return idx;
}
