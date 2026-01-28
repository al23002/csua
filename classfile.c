/*
 * Java Class File Format Generator Implementation
 */

#include "classfile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Class file magic number and default version
 */
static const uint32_t CLASSFILE_MAGIC = 0xCAFEBABEU;
static const int CLASSFILE_MINOR_VERSION = 0;
static const int CLASSFILE_MAJOR_VERSION = 61; /* Java 17 */

const char *CF_DESC_VOID = "V";
const char *CF_DESC_INT = "I";
const char *CF_DESC_LONG = "J";
const char *CF_DESC_FLOAT = "F";
const char *CF_DESC_DOUBLE = "D";
const char *CF_DESC_BYTE = "B";
const char *CF_DESC_CHAR = "C";
const char *CF_DESC_SHORT = "S";
const char *CF_DESC_BOOLEAN = "Z";

/* Type punning unions for float/double serialization */
typedef union FloatIntUnion_tag
{
    float f;
    uint32_t i;
} FloatIntUnion;

typedef union DoubleLongUnion_tag
{
    double d;
    uint64_t l;
} DoubleLongUnion;

/* ============================================================
 * Internal Buffer Writer
 * ============================================================ */

typedef struct CF_Writer_tag
{
    uint8_t *buffer;
    int size;
    int capacity;
} CF_Writer;

static void writer_init(CF_Writer *w)
{
    w->capacity = 4096;
    w->buffer = (uint8_t *)calloc(w->capacity, sizeof(uint8_t));
    w->size = 0;
}

static void writer_ensure(CF_Writer *w, int additional)
{
    if (w->size + additional > w->capacity)
    {
        while (w->size + additional > w->capacity)
        {
            w->capacity *= 2;
        }
        /* Manual reallocation (realloc not supported in Cminor) */
        uint8_t *new_buffer = (uint8_t *)calloc(w->capacity, sizeof(uint8_t));
        for (int i = 0; i < w->size; i++)
        {
            new_buffer[i] = w->buffer[i];
        }
        w->buffer = new_buffer;
    }
}

static void writer_u1(CF_Writer *w, uint8_t v)
{
    writer_ensure(w, 1);
    w->buffer[w->size++] = v;
}

static void writer_u2(CF_Writer *w, uint16_t v)
{
    writer_ensure(w, 2);
    w->buffer[w->size++] = (uint8_t)(v >> 8);
    w->buffer[w->size++] = (uint8_t)v;
}

static void writer_u4(CF_Writer *w, uint32_t v)
{
    writer_ensure(w, 4);
    w->buffer[w->size++] = (uint8_t)(v >> 24);
    w->buffer[w->size++] = (uint8_t)(v >> 16);
    w->buffer[w->size++] = (uint8_t)(v >> 8);
    w->buffer[w->size++] = (uint8_t)v;
}

static void writer_bytes(CF_Writer *w, const uint8_t *data, int len)
{
    writer_ensure(w, len);
    memcpy(&w->buffer[w->size], data, len);
    w->size += len;
}

/* ============================================================
 * Modified UTF-8 Encoding (MUTF-8)
 * ============================================================ */

/* Calculate MUTF-8 encoded length for UTF-8 input */
static int mutf8_encoded_len(const uint8_t *src, int src_len)
{
    int len = 0;
    for (int i = 0; i < src_len;)
    {
        uint8_t b = src[i];
        if (b == 0x00)
        {
            /* null → 0xC0 0x80 */
            len += 2;
            i++;
        }
        else if ((b & 0xF8U) == 0xF0U && i + 3 < src_len)
        {
            /* 4-byte UTF-8 → surrogate pair (6 bytes) */
            len += 6;
            i += 4;
        }
        else
        {
            len++;
            i++;
        }
    }
    return len;
}

/* Encode UTF-8 to MUTF-8, returns encoded length */
static int encode_mutf8(const uint8_t *src, int src_len, uint8_t *dst)
{
    int j = 0;
    for (int i = 0; i < src_len;)
    {
        uint8_t b = src[i];
        if (b == 0x00)
        {
            /* null → 0xC0 0x80 */
            dst[j++] = 0xC0U;
            dst[j++] = 0x80U;
            i++;
        }
        else if ((b & 0xF8U) == 0xF0U && i + 3 < src_len)
        {
            /* 4-byte UTF-8 → surrogate pair */
            int cp = ((b & 0x07U) << 18) |
                     ((src[i + 1] & 0x3FU) << 12) |
                     ((src[i + 2] & 0x3FU) << 6) |
                     (src[i + 3] & 0x3FU);
            cp -= 0x10000;
            int hi = 0xD800 + (cp >> 10);
            int lo = 0xDC00 + (cp & 0x3FF);
            /* Encode high surrogate as 3-byte CESU-8 */
            dst[j++] = 0xEDU;
            dst[j++] = (uint8_t)(0xA0U | ((hi >> 6) & 0x0FU));
            dst[j++] = (uint8_t)(0x80U | (hi & 0x3FU));
            /* Encode low surrogate as 3-byte CESU-8 */
            dst[j++] = 0xEDU;
            dst[j++] = (uint8_t)(0xB0U | ((lo >> 6) & 0x0FU));
            dst[j++] = (uint8_t)(0x80U | (lo & 0x3FU));
            i += 4;
        }
        else
        {
            dst[j++] = b;
            i++;
        }
    }
    return j;
}

/* ============================================================
 * Constant Pool Operations
 * ============================================================ */

CF_ConstantPool *cf_cp_create()
{
    CF_ConstantPool *cp = (CF_ConstantPool *)calloc(1, sizeof(CF_ConstantPool));
    cp->capacity = 64;
    cp->entries = (CF_ConstantEntry *)calloc(cp->capacity, sizeof(CF_ConstantEntry));
    cp->count = 1; /* Index 0 is unused in constant pool */
    return cp;
}

void cf_cp_free(CF_ConstantPool *cp)
{
    if (cp->entries == NULL)
    {
        return;
    }

    for (uint16_t i = 1; i < cp->count; ++i)
    {
        if (cp->entries[i].tag == CP_TAG_UTF8 && cp->entries[i].u.utf8.bytes != NULL)
        {
            free(cp->entries[i].u.utf8.bytes);
        }
    }
    free(cp->entries);
    cp->entries = NULL;
    cp->count = 0;
}

static int cf_cp_alloc(CF_ConstantPool *cp, int slots)
{
    if (cp->count + slots > cp->capacity)
    {
        cp->capacity = (uint16_t)(cp->capacity * 2);
        /* Manual reallocation (realloc not supported in Cminor) */
        CF_ConstantEntry *new_entries = (CF_ConstantEntry *)calloc(cp->capacity, sizeof(CF_ConstantEntry));
        for (int i = 0; i < cp->count; i++)
        {
            new_entries[i] = cp->entries[i];
        }
        cp->entries = new_entries;
    }
    uint16_t idx = cp->count;
    cp->count = (uint16_t)(cp->count + slots);
    return idx;
}

/* Find UTF8 entry by MUTF-8 encoded bytes */
static bool bytes_equal(const uint8_t *a, const uint8_t *b, int len)
{
    for (int i = 0; i < len; i++)
    {
        if (a[i] != b[i])
            return false;
    }
    return true;
}

static uint16_t cf_cp_find_utf8_bytes(CF_ConstantPool *cp, const uint8_t *bytes, int len)
{
    for (uint16_t i = 1; i < cp->count; ++i)
    {
        if (cp->entries[i].tag == CP_TAG_UTF8 &&
            cp->entries[i].u.utf8.length == len &&
            bytes_equal(cp->entries[i].u.utf8.bytes, bytes, len))
        {
            return i;
        }
    }
    return 0;
}

int cf_cp_find_utf8(CF_ConstantPool *cp, const char *str)
{
    int len = strlen(str);
    return cf_cp_find_utf8_bytes(cp, (const uint8_t *)str, len);
}

/* Add UTF-8 string with explicit length, encoding to MUTF-8 */
int cf_cp_add_utf8_len(CF_ConstantPool *cp, const uint8_t *data, int len)
{
    /* Encode to MUTF-8 */
    int mutf8_len = mutf8_encoded_len(data, len);
    uint8_t *mutf8_buf = (uint8_t *)calloc(mutf8_len, sizeof(uint8_t));
    encode_mutf8(data, len, mutf8_buf);

    /* Check for existing entry */
    int existing = cf_cp_find_utf8_bytes(cp, mutf8_buf, mutf8_len);
    if (existing != 0)
    {
        return existing;
    }

    int idx = cf_cp_alloc(cp, 1);
    CF_ConstantEntry *e = &cp->entries[idx];
    e->tag = CP_TAG_UTF8;
    e->u.utf8.length = (uint16_t)mutf8_len;
    e->u.utf8.bytes = mutf8_buf;
    return idx;
}

int cf_cp_add_utf8(CF_ConstantPool *cp, const char *str)
{
    int len = strlen(str);
    return cf_cp_add_utf8_len(cp, (const uint8_t *)str, len);
}

int cf_cp_add_integer(CF_ConstantPool *cp, int32_t value)
{
    int idx = cf_cp_alloc(cp, 1);
    CF_ConstantEntry *e = &cp->entries[idx];
    e->tag = CP_TAG_INTEGER;
    e->u.integer = value;
    return idx;
}

int cf_cp_add_long(CF_ConstantPool *cp, int64_t value)
{
    int idx = cf_cp_alloc(cp, 2);
    CF_ConstantEntry *e = &cp->entries[idx];
    e->tag = CP_TAG_LONG;
    e->u.long_val = value;
    /* Slot idx+1 is unusable (JVM spec) */
    cp->entries[idx + 1].tag = 0;
    return idx;
}

int cf_cp_add_float(CF_ConstantPool *cp, float value)
{
    int idx = cf_cp_alloc(cp, 1);
    CF_ConstantEntry *e = &cp->entries[idx];
    e->tag = CP_TAG_FLOAT;
    e->u.float_val = value;
    return idx;
}

int cf_cp_add_double(CF_ConstantPool *cp, double value)
{
    int idx = cf_cp_alloc(cp, 2);
    CF_ConstantEntry *e = &cp->entries[idx];
    e->tag = CP_TAG_DOUBLE;
    e->u.double_val = value;
    /* Slot idx+1 is unusable (JVM spec) */
    cp->entries[idx + 1].tag = 0;
    return idx;
}

int cf_cp_add_class(CF_ConstantPool *cp, const char *name)
{
    int name_index = cf_cp_add_utf8(cp, name);

    /* Check for existing class entry with same name */
    for (int i = 1; i < cp->count; ++i)
    {
        if (cp->entries[i].tag == CP_TAG_CLASS &&
            cp->entries[i].u.index == name_index)
        {
            return i;
        }
    }

    int idx = cf_cp_alloc(cp, 1);
    CF_ConstantEntry *e = &cp->entries[idx];
    e->tag = CP_TAG_CLASS;
    e->u.index = (uint16_t)name_index;
    return idx;
}

int cf_cp_add_string(CF_ConstantPool *cp, const char *str)
{
    int utf8_index = cf_cp_add_utf8(cp, str);
    int idx = cf_cp_alloc(cp, 1);
    CF_ConstantEntry *e = &cp->entries[idx];
    e->tag = CP_TAG_STRING;
    e->u.index = (uint16_t)utf8_index;
    return idx;
}

int cf_cp_add_string_len(CF_ConstantPool *cp, const uint8_t *data, int len)
{
    int utf8_index = cf_cp_add_utf8_len(cp, data, len);
    int idx = cf_cp_alloc(cp, 1);
    CF_ConstantEntry *e = &cp->entries[idx];
    e->tag = CP_TAG_STRING;
    e->u.index = (uint16_t)utf8_index;
    return idx;
}

int cf_cp_add_name_and_type(CF_ConstantPool *cp,
                            const char *name, const char *descriptor)
{
    int name_idx = cf_cp_add_utf8(cp, name);
    int desc_idx = cf_cp_add_utf8(cp, descriptor);

    /* Check for existing */
    for (int i = 1; i < cp->count; ++i)
    {
        if (cp->entries[i].tag == CP_TAG_NAME_AND_TYPE &&
            cp->entries[i].u.name_and_type.name_index == name_idx &&
            cp->entries[i].u.name_and_type.descriptor_index == desc_idx)
        {
            return i;
        }
    }

    int idx = cf_cp_alloc(cp, 1);
    CF_ConstantEntry *e = &cp->entries[idx];
    e->tag = CP_TAG_NAME_AND_TYPE;
    e->u.name_and_type.name_index = (uint16_t)name_idx;
    e->u.name_and_type.descriptor_index = (uint16_t)desc_idx;
    return idx;
}

int cf_cp_add_fieldref(CF_ConstantPool *cp,
                       const char *class_name,
                       const char *field_name,
                       const char *descriptor)
{
    int class_idx = cf_cp_add_class(cp, class_name);
    int nat_idx = cf_cp_add_name_and_type(cp, field_name, descriptor);

    int idx = cf_cp_alloc(cp, 1);
    CF_ConstantEntry *e = &cp->entries[idx];
    e->tag = CP_TAG_FIELDREF;
    e->u.ref.class_index = (uint16_t)class_idx;
    e->u.ref.name_and_type_index = (uint16_t)nat_idx;
    return idx;
}

int cf_cp_add_methodref(CF_ConstantPool *cp,
                        const char *class_name,
                        const char *method_name,
                        const char *descriptor)
{
    int class_idx = cf_cp_add_class(cp, class_name);
    int nat_idx = cf_cp_add_name_and_type(cp, method_name, descriptor);

    int idx = cf_cp_alloc(cp, 1);
    CF_ConstantEntry *e = &cp->entries[idx];
    e->tag = CP_TAG_METHODREF;
    e->u.ref.class_index = (uint16_t)class_idx;
    e->u.ref.name_and_type_index = (uint16_t)nat_idx;
    return idx;
}

/* ============================================================
 * Builder Operations
 * ============================================================ */

CF_Builder *cf_builder_create(const char *class_name)
{
    CF_Builder *builder = (CF_Builder *)calloc(1, sizeof(CF_Builder));
    builder->cf = (CF_ClassFile *)calloc(1, sizeof(CF_ClassFile));

    CF_ClassFile *cf = builder->cf;
    cf->magic = CLASSFILE_MAGIC;
    cf->minor_version = (uint16_t)CLASSFILE_MINOR_VERSION;
    cf->major_version = (uint16_t)CLASSFILE_MAJOR_VERSION;

    cf->constant_pool = cf_cp_create();

    /* Add "Code" attribute name */
    builder->code_attr_name_index = (uint16_t)cf_cp_add_utf8(cf->constant_pool, "Code");
    builder->stackmap_attr_name_index =
        (uint16_t)cf_cp_add_utf8(cf->constant_pool, "StackMapTable");

    /* Add this class */
    cf->this_class = (uint16_t)cf_cp_add_class(cf->constant_pool, class_name);
    builder->this_class_name_index = (uint16_t)cf_cp_find_utf8(cf->constant_pool, class_name);

    /* Default superclass: java/lang/Object */
    cf->super_class = (uint16_t)cf_cp_add_class(cf->constant_pool, "java/lang/Object");

    cf->access_flags = (uint16_t)(ACC_PUBLIC | ACC_SUPER);

    return builder;
}

CF_Builder *cf_builder_create_from_cp(const char *class_name, CF_ConstantPool *cp)
{
    CF_Builder *builder = (CF_Builder *)calloc(1, sizeof(CF_Builder));
    builder->cf = (CF_ClassFile *)calloc(1, sizeof(CF_ClassFile));

    CF_ClassFile *cf = builder->cf;
    cf->magic = CLASSFILE_MAGIC;
    cf->minor_version = (uint16_t)CLASSFILE_MINOR_VERSION;
    cf->major_version = (uint16_t)CLASSFILE_MAJOR_VERSION;

    /* Use provided constant pool (takes ownership) */
    cf->constant_pool = cp;

    /* Add "Code" attribute name */
    builder->code_attr_name_index = (uint16_t)cf_cp_add_utf8(cf->constant_pool, "Code");
    builder->stackmap_attr_name_index =
        (uint16_t)cf_cp_add_utf8(cf->constant_pool, "StackMapTable");

    /* Add this class */
    cf->this_class = (uint16_t)cf_cp_add_class(cf->constant_pool, class_name);
    builder->this_class_name_index = (uint16_t)cf_cp_find_utf8(cf->constant_pool, class_name);

    /* Default superclass: java/lang/Object */
    cf->super_class = (uint16_t)cf_cp_add_class(cf->constant_pool, "java/lang/Object");

    cf->access_flags = (uint16_t)(ACC_PUBLIC | ACC_SUPER);

    return builder;
}

void cf_builder_destroy(CF_Builder *builder)
{
    CF_ClassFile *cf = builder->cf;

    cf_cp_free(cf->constant_pool);

    if (cf->interfaces != NULL)
    {
        free(cf->interfaces);
    }

    for (uint16_t i = 0; i < cf->fields_count; ++i)
    {
        if (cf->fields[i].attributes != NULL)
        {
            for (uint16_t j = 0; j < cf->fields[i].attributes_count; ++j)
            {
                if (cf->fields[i].attributes[j].info != NULL)
                {
                    free(cf->fields[i].attributes[j].info);
                }
            }
            free(cf->fields[i].attributes);
        }
    }
    if (cf->fields != NULL)
    {
        free(cf->fields);
    }

    for (uint16_t i = 0; i < cf->methods_count; ++i)
    {
        if (cf->methods[i].code != NULL)
        {
            if (cf->methods[i].code->code != NULL)
            {
                free(cf->methods[i].code->code);
            }
            if (cf->methods[i].code->exception_table != NULL)
            {
                free(cf->methods[i].code->exception_table);
            }
            if (cf->methods[i].code->stack_map_frames != NULL)
            {
                for (uint16_t f = 0; f < cf->methods[i].code->stack_map_frame_count; ++f)
                {
                    CF_StackMapFrame *frame = &cf->methods[i].code->stack_map_frames[f];
                    if (frame->locals != NULL)
                    {
                        free(frame->locals);
                    }
                    if (frame->stack != NULL)
                    {
                        free(frame->stack);
                    }
                }
                free(cf->methods[i].code->stack_map_frames);
            }
            if (cf->methods[i].code->attributes != NULL)
            {
                free(cf->methods[i].code->attributes);
            }
            free(cf->methods[i].code);
        }
        if (cf->methods[i].attributes != NULL)
        {
            free(cf->methods[i].attributes);
        }
    }
    if (cf->methods != NULL)
    {
        free(cf->methods);
    }

    for (uint16_t i = 0; i < cf->attributes_count; ++i)
    {
        if (cf->attributes[i].info != NULL)
        {
            free(cf->attributes[i].info);
        }
    }
    if (cf->attributes != NULL)
    {
        free(cf->attributes);
    }
}

bool cf_builder_write_to_file(CF_Builder *builder, const char *filename)
{
    return cf_write_to_file(builder->cf, filename);
}

void cf_builder_set_super(CF_Builder *builder, const char *super_name)
{
    builder->cf->super_class = (uint16_t)cf_cp_add_class(builder->cf->constant_pool, super_name);
}

void cf_builder_add_field(CF_Builder *builder,
                          int access_flags,
                          const char *name,
                          const char *descriptor)
{
    CF_ClassFile *cf = builder->cf;

    cf->fields_count++;
    /* Manual reallocation (realloc not supported in Cminor) */
    CF_FieldInfo *new_fields = (CF_FieldInfo *)calloc(cf->fields_count, sizeof(CF_FieldInfo));
    for (int i = 0; i < cf->fields_count - 1; i++)
    {
        new_fields[i] = cf->fields[i];
    }
    cf->fields = new_fields;

    CF_FieldInfo *f = &cf->fields[cf->fields_count - 1];
    /* calloc already zero-initializes */
    f->access_flags = (uint16_t)access_flags;
    f->name_index = (uint16_t)cf_cp_add_utf8(cf->constant_pool, name);
    f->descriptor_index = (uint16_t)cf_cp_add_utf8(cf->constant_pool, descriptor);
}

int cf_builder_begin_method(CF_Builder *builder,
                            int access_flags,
                            const char *name,
                            const char *descriptor)
{
    CF_ClassFile *cf = builder->cf;

    cf->methods_count++;
    /* Manual reallocation (realloc not supported in Cminor) */
    CF_MethodInfo *new_methods = (CF_MethodInfo *)calloc(cf->methods_count, sizeof(CF_MethodInfo));
    for (int i = 0; i < cf->methods_count - 1; i++)
    {
        new_methods[i] = cf->methods[i];
    }
    cf->methods = new_methods;

    CF_MethodInfo *m = &cf->methods[cf->methods_count - 1];
    /* calloc already zero-initializes */
    m->access_flags = (uint16_t)access_flags;
    m->name_index = (uint16_t)cf_cp_add_utf8(cf->constant_pool, name);
    m->descriptor_index = (uint16_t)cf_cp_add_utf8(cf->constant_pool, descriptor);

    return cf->methods_count - 1;
}

void cf_builder_set_code(CF_Builder *builder,
                         int method_index,
                         int max_stack,
                         int max_locals,
                         uint8_t *code,
                         int code_length)
{
    CF_ClassFile *cf = builder->cf;
    if (method_index >= cf->methods_count)
    {
        return;
    }

    CF_MethodInfo *m = &cf->methods[method_index];

    /* Create Code attribute */
    m->code = (CF_CodeAttribute *)calloc(1, sizeof(CF_CodeAttribute));
    m->code->attribute_name_index = builder->code_attr_name_index;
    m->code->max_stack = (uint16_t)max_stack;
    m->code->max_locals = (uint16_t)max_locals;
    m->code->code_length = (uint32_t)code_length;
    m->code->has_stack_map_table = false;
    m->code->stack_map_table_name_index = 0;
    m->code->stack_map_frame_count = 0;

    if (code_length > 0)
    {
        m->code->code = (uint8_t *)calloc(code_length, sizeof(uint8_t));
        memcpy(m->code->code, code, code_length);
    }

    /* Method has one attribute: Code */
    m->attributes_count = 1;
    m->attributes = (CF_Attribute *)calloc(1, sizeof(CF_Attribute));
    m->attributes[0].attribute_name_index = builder->code_attr_name_index;
}

void cf_builder_set_stack_map_table(CF_Builder *builder,
                                    int method_index,
                                    CF_StackMapFrame *frames,
                                    int frame_count)
{
    if (method_index >= builder->cf->methods_count)
    {
        return;
    }

    CF_MethodInfo *m = &builder->cf->methods[method_index];
    if (m->code == NULL)
    {
        return;
    }

    /* No frames to add */
    if (frames == NULL || frame_count == 0)
    {
        return;
    }

    /* Add StackMapTable name to constant pool if not already added */
    int smt_name_index = cf_cp_add_utf8(builder->cf->constant_pool, "StackMapTable");

    m->code->has_stack_map_table = true;
    m->code->stack_map_table_name_index = (uint16_t)smt_name_index;
    m->code->stack_map_frame_count = (uint16_t)frame_count;

    /* Copy frames */
    m->code->stack_map_frames = (CF_StackMapFrame *)calloc(frame_count, sizeof(CF_StackMapFrame));
    for (int i = 0; i < frame_count; ++i)
    {
        m->code->stack_map_frames[i] = frames[i];

        /* Deep copy locals and stack arrays */
        if (frames[i].locals_count > 0 && frames[i].locals != NULL)
        {
            m->code->stack_map_frames[i].locals = (CF_VerificationTypeInfo *)calloc(
                frames[i].locals_count, sizeof(CF_VerificationTypeInfo));
            for (int j = 0; j < frames[i].locals_count; j++)
                m->code->stack_map_frames[i].locals[j] = frames[i].locals[j];
        }
        if (frames[i].stack_count > 0 && frames[i].stack != NULL)
        {
            m->code->stack_map_frames[i].stack = (CF_VerificationTypeInfo *)calloc(
                frames[i].stack_count, sizeof(CF_VerificationTypeInfo));
            for (int j = 0; j < frames[i].stack_count; j++)
                m->code->stack_map_frames[i].stack[j] = frames[i].stack[j];
        }
    }
}

void cf_builder_set_line_number_table(CF_Builder *builder,
                                      int method_index,
                                      CF_LineNumberEntry *entries,
                                      int entry_count)
{
    if (method_index >= builder->cf->methods_count)
    {
        return;
    }

    CF_MethodInfo *m = &builder->cf->methods[method_index];
    if (m->code == NULL)
    {
        return;
    }

    /* No entries to add */
    if (entries == NULL || entry_count == 0)
    {
        return;
    }

    /* Add LineNumberTable name to constant pool if not already added */
    int lnt_name_index = cf_cp_add_utf8(builder->cf->constant_pool, "LineNumberTable");

    m->code->has_line_number_table = true;
    m->code->line_number_table_name_index = (uint16_t)lnt_name_index;
    m->code->line_number_count = (uint16_t)entry_count;

    /* Copy entries */
    m->code->line_numbers = (CF_LineNumberEntry *)calloc(entry_count, sizeof(CF_LineNumberEntry));
    for (int i = 0; i < entry_count; ++i)
    {
        m->code->line_numbers[i].start_pc = entries[i].start_pc;
        m->code->line_numbers[i].line_number = entries[i].line_number;
    }
}

void cf_builder_set_source_file(CF_Builder *builder, const char *source_file)
{
    if (!builder || !source_file)
    {
        return;
    }

    /* Add "SourceFile" attribute name and source file name to constant pool */
    cf_cp_add_utf8(builder->cf->constant_pool, "SourceFile");
    int name_index = cf_cp_add_utf8(builder->cf->constant_pool, source_file);

    builder->cf->has_source_file = true;
    builder->cf->source_file_name_index = (uint16_t)name_index;
}

/* ============================================================
 * Output Operations
 * ============================================================ */

static void write_constant_pool(CF_Writer *w, CF_ConstantPool *cp)
{
    writer_u2(w, cp->count);

    for (uint16_t i = 1; i < cp->count; ++i)
    {
        CF_ConstantEntry *e = &cp->entries[i];

        /* Skip placeholder slots for Long/Double */
        if (e->tag == 0)
        {
            continue;
        }

        writer_u1(w, (uint8_t)e->tag);

        switch (e->tag)
        {
        case CP_TAG_UTF8:
            writer_u2(w, e->u.utf8.length);
            writer_bytes(w, e->u.utf8.bytes, e->u.utf8.length);
            break;

        case CP_TAG_INTEGER:
            writer_u4(w, e->u.integer);
            break;

        case CP_TAG_FLOAT:
        {
            /* Type punning via union (memcpy not supported in Cminor) */
            FloatIntUnion u;
            u.f = e->u.float_val;
            writer_u4(w, u.i);
            break;
        }

        case CP_TAG_LONG:
        {
            uint64_t val = (uint64_t)e->u.long_val;
            writer_u4(w, (uint32_t)(val >> 32));
            writer_u4(w, (uint32_t)(val & 0xffffffffU));
            break;
        }

        case CP_TAG_DOUBLE:
        {
            /* Type punning via union (memcpy not supported in Cminor) */
            DoubleLongUnion u;
            u.d = e->u.double_val;
            writer_u4(w, (uint32_t)(u.l >> 32));
            writer_u4(w, (uint32_t)(u.l & 0xffffffffU));
            break;
        }

        case CP_TAG_CLASS:
        case CP_TAG_STRING:
        case CP_TAG_METHOD_TYPE:
            writer_u2(w, e->u.index);
            break;

        case CP_TAG_FIELDREF:
        case CP_TAG_METHODREF:
        case CP_TAG_INTERFACE_METHODREF:
            writer_u2(w, e->u.ref.class_index);
            writer_u2(w, e->u.ref.name_and_type_index);
            break;

        case CP_TAG_NAME_AND_TYPE:
            writer_u2(w, e->u.name_and_type.name_index);
            writer_u2(w, e->u.name_and_type.descriptor_index);
            break;

        case CP_TAG_METHOD_HANDLE:
            writer_u1(w, e->u.method_handle.reference_kind);
            writer_u2(w, e->u.method_handle.reference_index);
            break;

        case CP_TAG_INVOKE_DYNAMIC:
            writer_u2(w, e->u.invoke_dynamic.bootstrap_method_attr_index);
            writer_u2(w, e->u.invoke_dynamic.name_and_type_index);
            break;

        default:
            break;
        }
    }
}

static int verification_type_size(CF_VerificationTypeInfo *info)
{
    switch (info->tag)
    {
    case CF_VERIFICATION_OBJECT:
    case CF_VERIFICATION_UNINITIALIZED:
        return 3; /* tag + u2 */
    default:
        return 1; /* only tag */
    }
}

static void write_verification_type(CF_Writer *w, CF_VerificationTypeInfo *info)
{
    writer_u1(w, (uint8_t)info->tag);
    if (info->tag == CF_VERIFICATION_OBJECT)
    {
        writer_u2(w, info->u.cpool_index);
    }
    else if (info->tag == CF_VERIFICATION_UNINITIALIZED)
    {
        writer_u2(w, info->u.offset);
    }
}

static int stack_map_frame_size(CF_StackMapFrame *frame)
{
    uint8_t type = frame->frame_type;
    if (type <= 63)
    {
        return 1;
    }
    if (type >= 64 && type <= 127)
    {
        return 1 + verification_type_size(&frame->stack[0]);
    }
    if (type == 247)
    {
        return 1 + 2 + verification_type_size(&frame->stack[0]);
    }
    if (type >= 248 && type <= 250)
    {
        return 1 + 2; /* chop frame */
    }
    if (type == 251)
    {
        return 1 + 2; /* same_frame_extended */
    }
    if (type >= 252 && type <= 254)
    {
        uint32_t size = 1 + 2;
        uint16_t k = (uint16_t)(type - 251);
        for (uint16_t i = 0; i < k; ++i)
        {
            size += verification_type_size(&frame->locals[i]);
        }
        return size;
    }
    /* full_frame (255) */
    uint32_t size = 1 + 2 + 2; /* frame_type + offset_delta + number_of_locals */
    for (uint16_t i = 0; i < frame->locals_count; ++i)
    {
        size += verification_type_size(&frame->locals[i]);
    }
    size += 2; /* number_of_stack_items */
    for (uint16_t i = 0; i < frame->stack_count; ++i)
    {
        size += verification_type_size(&frame->stack[i]);
    }
    return size;
}

static void write_stack_map_frame(CF_Writer *w, CF_StackMapFrame *frame)
{
    uint8_t type = frame->frame_type;
    writer_u1(w, type);

    if (type <= 63)
    {
        return; /* same_frame */
    }
    if (type >= 64 && type <= 127)
    {
        write_verification_type(w, &frame->stack[0]);
        return;
    }
    if (type == 247)
    {
        writer_u2(w, frame->offset_delta);
        write_verification_type(w, &frame->stack[0]);
        return;
    }
    if (type >= 248 && type <= 250)
    {
        writer_u2(w, frame->offset_delta);
        return;
    }
    if (type == 251)
    {
        writer_u2(w, frame->offset_delta);
        return;
    }
    if (type >= 252 && type <= 254)
    {
        writer_u2(w, frame->offset_delta);
        uint16_t k = (uint16_t)(type - 251);
        for (uint16_t i = 0; i < k; ++i)
        {
            write_verification_type(w, &frame->locals[i]);
        }
        return;
    }

    /* full_frame */
    writer_u2(w, frame->offset_delta);
    writer_u2(w, frame->locals_count);
    for (uint16_t i = 0; i < frame->locals_count; ++i)
    {
        write_verification_type(w, &frame->locals[i]);
    }
    writer_u2(w, frame->stack_count);
    for (uint16_t i = 0; i < frame->stack_count; ++i)
    {
        write_verification_type(w, &frame->stack[i]);
    }
}

static int stack_map_table_attribute_length(CF_CodeAttribute *code)
{
    if (code->has_stack_map_table == false)
    {
        return 0;
    }

    uint32_t size = 2; /* number_of_entries */
    for (uint16_t i = 0; i < code->stack_map_frame_count; ++i)
    {
        size += stack_map_frame_size(&code->stack_map_frames[i]);
    }
    return size;
}

static void write_code_attribute(CF_Writer *w, CF_CodeAttribute *code)
{
    /* Calculate attribute length:
     * 2 (max_stack) + 2 (max_locals) + 4 (code_length) + code_length
     * + 2 (exception_table_length) + 8 * exception_table_length
     * + 2 (attributes_count) + attributes
     */
    uint16_t nested_attr_count = (uint16_t)(code->attributes_count +
                                            (code->has_stack_map_table ? 1 : 0) +
                                            (code->has_line_number_table ? 1 : 0));

    uint32_t nested_attr_size = 0;
    if (code->has_stack_map_table)
    {
        uint32_t smt_len = stack_map_table_attribute_length(code);
        nested_attr_size += 2 + 4 + smt_len;
    }
    if (code->has_line_number_table)
    {
        /* LineNumberTable: 2 (name_index) + 4 (length) + 2 (count) + 4 * count */
        uint32_t lnt_len = 2 + 4 * code->line_number_count;
        nested_attr_size += 2 + 4 + lnt_len;
    }
    for (uint16_t i = 0; i < code->attributes_count; ++i)
    {
        nested_attr_size += 2 + 4 + code->attributes[i].attribute_length;
    }

    uint32_t attr_length = 2 + 2 + 4 + code->code_length + 2 + 8 * code->exception_table_length + 2 + nested_attr_size;

    writer_u2(w, code->attribute_name_index);
    writer_u4(w, attr_length);
    writer_u2(w, code->max_stack);
    writer_u2(w, code->max_locals);
    writer_u4(w, code->code_length);
    if (code->code_length > 0U)
    {
        writer_bytes(w, code->code, code->code_length);
    }

    writer_u2(w, code->exception_table_length);
    for (uint16_t i = 0; i < code->exception_table_length; ++i)
    {
        CF_ExceptionEntry *ex = &code->exception_table[i];
        writer_u2(w, ex->start_pc);
        writer_u2(w, ex->end_pc);
        writer_u2(w, ex->handler_pc);
        writer_u2(w, ex->catch_type);
    }

    writer_u2(w, nested_attr_count);

    if (code->has_stack_map_table)
    {
        uint32_t smt_len = stack_map_table_attribute_length(code);
        writer_u2(w, code->stack_map_table_name_index);
        writer_u4(w, smt_len);
        writer_u2(w, code->stack_map_frame_count);
        for (uint16_t i = 0; i < code->stack_map_frame_count; ++i)
        {
            write_stack_map_frame(w, &code->stack_map_frames[i]);
        }
    }

    if (code->has_line_number_table)
    {
        uint32_t lnt_len = 2 + 4 * code->line_number_count;
        writer_u2(w, code->line_number_table_name_index);
        writer_u4(w, lnt_len);
        writer_u2(w, code->line_number_count);
        for (uint16_t i = 0; i < code->line_number_count; ++i)
        {
            writer_u2(w, (uint16_t)code->line_numbers[i].start_pc);
            writer_u2(w, (uint16_t)code->line_numbers[i].line_number);
        }
    }

    for (uint16_t i = 0; i < code->attributes_count; ++i)
    {
        CF_Attribute *a = &code->attributes[i];
        writer_u2(w, a->attribute_name_index);
        writer_u4(w, a->attribute_length);
        if (a->attribute_length > 0U)
        {
            writer_bytes(w, a->info, a->attribute_length);
        }
    }
}

static void write_field(CF_Writer *w, CF_FieldInfo *f)
{
    writer_u2(w, f->access_flags);
    writer_u2(w, f->name_index);
    writer_u2(w, f->descriptor_index);
    writer_u2(w, f->attributes_count);

    for (uint16_t i = 0; i < f->attributes_count; ++i)
    {
        CF_Attribute *a = &f->attributes[i];
        writer_u2(w, a->attribute_name_index);
        writer_u4(w, a->attribute_length);
        if (a->attribute_length > 0U)
        {
            writer_bytes(w, a->info, a->attribute_length);
        }
    }
}

static void write_method(CF_Writer *w, CF_MethodInfo *m)
{
    writer_u2(w, m->access_flags);
    writer_u2(w, m->name_index);
    writer_u2(w, m->descriptor_index);

    /* Count attributes (Code is one) */
    uint16_t attr_count = (uint16_t)(m->code != NULL ? 1 : 0);
    writer_u2(w, attr_count);

    if (m->code != NULL)
    {
        write_code_attribute(w, m->code);
    }
}

int cf_write_to_buffer(CF_ClassFile *cf, uint8_t **buffer)
{
    CF_Writer w;
    writer_init(&w);

    /* Magic */
    writer_u4(&w, cf->magic);

    /* Version */
    writer_u2(&w, cf->minor_version);
    writer_u2(&w, cf->major_version);

    /* Constant Pool */
    write_constant_pool(&w, cf->constant_pool);

    /* Access Flags, This Class, Super Class */
    writer_u2(&w, cf->access_flags);
    writer_u2(&w, cf->this_class);
    writer_u2(&w, cf->super_class);

    /* Interfaces */
    writer_u2(&w, cf->interfaces_count);
    for (uint16_t i = 0; i < cf->interfaces_count; ++i)
    {
        writer_u2(&w, cf->interfaces[i]);
    }

    /* Fields */
    writer_u2(&w, cf->fields_count);
    for (uint16_t i = 0; i < cf->fields_count; ++i)
    {
        write_field(&w, &cf->fields[i]);
    }

    /* Methods */
    writer_u2(&w, cf->methods_count);
    for (uint16_t i = 0; i < cf->methods_count; ++i)
    {
        write_method(&w, &cf->methods[i]);
    }

    /* Class Attributes */
    uint16_t total_class_attrs = cf->attributes_count;
    if (cf->has_source_file)
    {
        total_class_attrs++;
    }
    writer_u2(&w, total_class_attrs);

    /* Write SourceFile attribute if present */
    if (cf->has_source_file)
    {
        int sf_name_idx = cf_cp_add_utf8(cf->constant_pool, "SourceFile");
        writer_u2(&w, (uint16_t)sf_name_idx);
        writer_u4(&w, 2); /* attribute_length = 2 */
        writer_u2(&w, cf->source_file_name_index);
    }

    for (uint16_t i = 0; i < cf->attributes_count; ++i)
    {
        CF_Attribute *a = &cf->attributes[i];
        writer_u2(&w, a->attribute_name_index);
        writer_u4(&w, a->attribute_length);
        if (a->attribute_length > 0U)
        {
            writer_bytes(&w, a->info, a->attribute_length);
        }
    }

    *buffer = w.buffer;
    return w.size;
}

bool cf_write_to_file(CF_ClassFile *cf, const char *filename)
{
    uint8_t *buffer = NULL;
    int size = cf_write_to_buffer(cf, &buffer);

    FILE *fp = fopen(filename, "wb");
    if (fp == NULL)
    {
        free(buffer);
        return false;
    }

    int written = fwrite(buffer, 1, size, fp);
    fclose(fp);
    free(buffer);

    return written == size;
}

/* ============================================================
 * Descriptor Utilities
 * ============================================================ */

char *cf_build_method_descriptor(const char *return_type,
                                 const char *param_types)
{
    int param_len = param_types ? strlen(param_types) : 0;
    int ret_len = strlen(return_type);
    int total = 1 + param_len + 1 + ret_len + 1; /* ( params ) return \0 */

    char *desc = (char *)calloc(total, sizeof(char));
    desc[0] = '(';
    if (param_len > 0)
    {
        strncpy(&desc[1], param_types, param_len);
    }
    desc[1 + param_len] = ')';
    strncpy(&desc[2 + param_len], return_type, ret_len);
    desc[total - 1] = '\0';

    return desc;
}

char *cf_desc_array(const char *element_type)
{
    int len = strlen(element_type);
    char *desc = (char *)calloc(len + 2, sizeof(char));
    desc[0] = '[';
    strncpy(&desc[1], element_type, len);
    desc[len + 1] = '\0';
    return desc;
}

char *cf_desc_object(const char *class_name)
{
    int len = strlen(class_name);
    char *desc = (char *)calloc(len + 3, sizeof(char));
    desc[0] = 'L';
    strncpy(&desc[1], class_name, len);
    desc[len + 1] = ';';
    desc[len + 2] = '\0';
    return desc;
}
