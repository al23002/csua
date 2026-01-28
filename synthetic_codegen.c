/*
 * Synthetic Class Code Generator
 *
 * Generates pointer struct classes (__intPtr, __charPtr, etc.) programmatically.
 */

#include "synthetic_codegen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "classfile.h"
#include "classfile_opcode.h"

PtrUsage *g_ptr_usage;

/* Pointer type definitions - initialized in init_ptr_types() */
static PtrTypeInfo *PTR_TYPES = NULL;
static bool ptr_types_initialized = false;

static void init_ptr_types()
{
    if (ptr_types_initialized)
        return;

    /* Allocate the array */
    PTR_TYPES = (PtrTypeInfo *)calloc(PTR_TYPE_COUNT, sizeof(PtrTypeInfo));

    PTR_TYPES[0].suffix = "_char";
    PTR_TYPES[0].class_name = "__charPtr";
    PTR_TYPES[0].base_desc = "[B";
    PTR_TYPES[0].elem_desc = "B";
    PTR_TYPES[0].class_desc = "L__charPtr;";
    PTR_TYPES[0].aload_opcode = CF_BALOAD;
    PTR_TYPES[0].astore_opcode = CF_BASTORE;
    PTR_TYPES[0].return_opcode = CF_IRETURN;
    PTR_TYPES[0].is_wide = false;

    PTR_TYPES[1].suffix = "_bool";
    PTR_TYPES[1].class_name = "__boolPtr";
    PTR_TYPES[1].base_desc = "[Z";
    PTR_TYPES[1].elem_desc = "Z";
    PTR_TYPES[1].class_desc = "L__boolPtr;";
    PTR_TYPES[1].aload_opcode = CF_BALOAD;
    PTR_TYPES[1].astore_opcode = CF_BASTORE;
    PTR_TYPES[1].return_opcode = CF_IRETURN;
    PTR_TYPES[1].is_wide = false;

    PTR_TYPES[2].suffix = "_short";
    PTR_TYPES[2].class_name = "__shortPtr";
    PTR_TYPES[2].base_desc = "[S";
    PTR_TYPES[2].elem_desc = "S";
    PTR_TYPES[2].class_desc = "L__shortPtr;";
    PTR_TYPES[2].aload_opcode = CF_SALOAD;
    PTR_TYPES[2].astore_opcode = CF_SASTORE;
    PTR_TYPES[2].return_opcode = CF_IRETURN;
    PTR_TYPES[2].is_wide = false;

    PTR_TYPES[3].suffix = "_int";
    PTR_TYPES[3].class_name = "__intPtr";
    PTR_TYPES[3].base_desc = "[I";
    PTR_TYPES[3].elem_desc = "I";
    PTR_TYPES[3].class_desc = "L__intPtr;";
    PTR_TYPES[3].aload_opcode = CF_IALOAD;
    PTR_TYPES[3].astore_opcode = CF_IASTORE;
    PTR_TYPES[3].return_opcode = CF_IRETURN;
    PTR_TYPES[3].is_wide = false;

    PTR_TYPES[4].suffix = "_long";
    PTR_TYPES[4].class_name = "__longPtr";
    PTR_TYPES[4].base_desc = "[J";
    PTR_TYPES[4].elem_desc = "J";
    PTR_TYPES[4].class_desc = "L__longPtr;";
    PTR_TYPES[4].aload_opcode = CF_LALOAD;
    PTR_TYPES[4].astore_opcode = CF_LASTORE;
    PTR_TYPES[4].return_opcode = CF_LRETURN;
    PTR_TYPES[4].is_wide = true;

    PTR_TYPES[5].suffix = "_float";
    PTR_TYPES[5].class_name = "__floatPtr";
    PTR_TYPES[5].base_desc = "[F";
    PTR_TYPES[5].elem_desc = "F";
    PTR_TYPES[5].class_desc = "L__floatPtr;";
    PTR_TYPES[5].aload_opcode = CF_FALOAD;
    PTR_TYPES[5].astore_opcode = CF_FASTORE;
    PTR_TYPES[5].return_opcode = CF_FRETURN;
    PTR_TYPES[5].is_wide = false;

    PTR_TYPES[6].suffix = "_double";
    PTR_TYPES[6].class_name = "__doublePtr";
    PTR_TYPES[6].base_desc = "[D";
    PTR_TYPES[6].elem_desc = "D";
    PTR_TYPES[6].class_desc = "L__doublePtr;";
    PTR_TYPES[6].aload_opcode = CF_DALOAD;
    PTR_TYPES[6].astore_opcode = CF_DASTORE;
    PTR_TYPES[6].return_opcode = CF_DRETURN;
    PTR_TYPES[6].is_wide = true;

    PTR_TYPES[7].suffix = "_object";
    PTR_TYPES[7].class_name = "__objectPtr";
    PTR_TYPES[7].base_desc = "[Ljava/lang/Object;";
    PTR_TYPES[7].elem_desc = "Ljava/lang/Object;";
    PTR_TYPES[7].class_desc = "L__objectPtr;";
    PTR_TYPES[7].aload_opcode = CF_AALOAD;
    PTR_TYPES[7].astore_opcode = CF_AASTORE;
    PTR_TYPES[7].return_opcode = CF_ARETURN;
    PTR_TYPES[7].is_wide = false;

    ptr_types_initialized = true;
}

/*
 * Usage tracking functions
 */

void ptr_usage_init(PtrUsage *usage)
{
    /* Allocate the array if not already allocated */
    if (usage->used == NULL)
    {
        usage->used = (bool *)calloc(PTR_TYPE_COUNT, sizeof(bool));
    }
    for (int i = 0; i < PTR_TYPE_COUNT; i++)
    {
        usage->used[i] = false;
    }
}

void ptr_usage_mark(PtrTypeIndex type)
{
    if (type < 0 || type >= PTR_TYPE_COUNT)
        return;
    /* Auto-initialize if needed */
    if (g_ptr_usage->used == NULL)
    {
        g_ptr_usage->used = (bool *)calloc(PTR_TYPE_COUNT, sizeof(bool));
    }
    g_ptr_usage->used[type] = true;
}

bool ptr_usage_any(const PtrUsage *usage)
{
    for (int i = 0; i < PTR_TYPE_COUNT; i++)
    {
        if (usage->used[i])
        {
            return true;
        }
    }
    return false;
}

void generate_ptr_struct_classes_selective(const PtrUsage *usage)
{
    init_ptr_types();
    for (int i = 0; i < PTR_TYPE_COUNT; i++)
    {
        if (!usage->used[i])
            continue;

        const PtrTypeInfo *info = &PTR_TYPES[i];

        CF_Builder *builder = cf_builder_create(info->class_name);

        cf_builder_add_field(builder, ACC_PUBLIC, "base", info->base_desc);
        cf_builder_add_field(builder, ACC_PUBLIC, "offset", "I");

        int object_init_idx = cf_cp_add_methodref(builder->cf->constant_pool, "java/lang/Object", "<init>", "()V");
        int init_method_idx = cf_builder_begin_method(builder, ACC_PUBLIC, "<init>", "()V");
        uint8_t init_code[5];
        init_code[0] = CF_ALOAD_0;
        init_code[1] = CF_INVOKESPECIAL;
        init_code[2] = (uint8_t)(object_init_idx >> 8);
        init_code[3] = (uint8_t)object_init_idx;
        init_code[4] = CF_RETURN;

        cf_builder_set_code(builder, init_method_idx, 1, 1, init_code, 5);

        char output_path[64];
        snprintf(output_path, sizeof output_path, "%s.class", info->class_name);

        if (!cf_builder_write_to_file(builder, output_path))
        {
            fprintf(stderr, "failed to write %s\n", output_path);
            exit(1);
        }

        cf_builder_destroy(builder);
    }
}

/*
 * Methodref API for codegenvisitor
 *
 * These functions add methodrefs to the constant pool and automatically
 * mark the usage for selective class generation.
 */

const char *ptr_type_descriptor(PtrTypeIndex type)
{
    if (type < 0 || type >= PTR_TYPE_COUNT)
    {
        fprintf(stderr, "ptr_type_descriptor: invalid type index %d\n", type);
        exit(1);
    }
    init_ptr_types();
    return PTR_TYPES[type].class_desc;
}

const char *ptr_type_class_name(PtrTypeIndex type)
{
    if (type < 0 || type >= PTR_TYPE_COUNT)
    {
        fprintf(stderr, "ptr_type_class_name: invalid type index %d\n", type);
        exit(1);
    }
    init_ptr_types();
    return PTR_TYPES[type].class_name;
}

PtrTypeIndex ptr_type_index_from_jvm_tag(char tag)
{
    switch (tag)
    {
    case 'B':
        return PTR_TYPE_CHAR;
    case 'Z':
        return PTR_TYPE_BOOL;
    case 'S':
        return PTR_TYPE_SHORT;
    case 'I':
        return PTR_TYPE_INT;
    case 'J':
        return PTR_TYPE_LONG;
    case 'F':
        return PTR_TYPE_FLOAT;
    case 'D':
        return PTR_TYPE_DOUBLE;
    case 'L':
        return PTR_TYPE_OBJECT;
    default:
        fprintf(stderr, "ptr_type_index_from_jvm_tag: invalid JVM tag '%c' (0x%02x)\n", tag, (unsigned char)tag);
        exit(1);
    }
}

const char *ptr_type_base_descriptor(PtrTypeIndex type)
{
    if (type < 0 || type >= PTR_TYPE_COUNT)
    {
        fprintf(stderr, "ptr_type_base_descriptor: invalid type index %d\n", type);
        exit(1);
    }
    init_ptr_types();
    return PTR_TYPES[type].base_desc;
}

const char *ptr_type_elem_descriptor(PtrTypeIndex type)
{
    if (type < 0 || type >= PTR_TYPE_COUNT)
    {
        fprintf(stderr, "ptr_type_elem_descriptor: invalid type index %d\n", type);
        exit(1);
    }
    init_ptr_types();
    return PTR_TYPES[type].elem_desc;
}

uint8_t ptr_type_aload_opcode(PtrTypeIndex type)
{
    if (type < 0 || type >= PTR_TYPE_COUNT)
    {
        fprintf(stderr, "ptr_type_aload_opcode: invalid type index %d\n", type);
        exit(1);
    }
    init_ptr_types();
    return PTR_TYPES[type].aload_opcode;
}

uint8_t ptr_type_astore_opcode(PtrTypeIndex type)
{
    if (type < 0 || type >= PTR_TYPE_COUNT)
    {
        fprintf(stderr, "ptr_type_astore_opcode: invalid type index %d\n", type);
        exit(1);
    }
    init_ptr_types();
    return PTR_TYPES[type].astore_opcode;
}

bool ptr_type_is_wide(PtrTypeIndex type)
{
    if (type < 0 || type >= PTR_TYPE_COUNT)
        return false;
    init_ptr_types();
    return PTR_TYPES[type].is_wide;
}
