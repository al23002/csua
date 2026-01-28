#pragma once

#include "classfile.h"
#include "constant_pool.h"

/*
 * Synthetic Class Code Generator
 *
 * Generates runtime support classes programmatically using CF_Builder,
 * replacing the need for C source files in the synthetic/ directory.
 */

/* Pointer type info for code generation */
typedef struct PtrTypeInfo_tag
{
    const char *suffix;     /* e.g., "_int", "_char" */
    const char *class_name; /* e.g., "__intPtr" */
    const char *base_desc;  /* e.g., "[I" for int array */
    const char *elem_desc;  /* e.g., "I" for int */
    const char *class_desc; /* e.g., "L__intPtr;" */
    uint8_t aload_opcode;   /* iaload, baload, etc. */
    uint8_t astore_opcode;  /* iastore, bastore, etc. */
    uint8_t return_opcode;  /* ireturn, lreturn, etc. */
    bool is_wide;           /* true for long/double (2 slots) */
} PtrTypeInfo;

/* Pointer type indices */
typedef enum
{
    PTR_TYPE_CHAR = 0,
    PTR_TYPE_BOOL,
    PTR_TYPE_SHORT,
    PTR_TYPE_INT,
    PTR_TYPE_LONG,
    PTR_TYPE_FLOAT,
    PTR_TYPE_DOUBLE,
    PTR_TYPE_OBJECT,
    PTR_TYPE_COUNT
} PtrTypeIndex;

/* Usage tracking for lazy class generation */
typedef struct PtrUsage_tag
{
    bool *used;
} PtrUsage;

extern PtrUsage *g_ptr_usage;

/* Initialize usage tracking */
void ptr_usage_init(PtrUsage *usage);

/* Mark a pointer type as used (safe, auto-initializes if needed) */
void ptr_usage_mark(PtrTypeIndex type);

/* Check if any pointer operations are used */
bool ptr_usage_any(const PtrUsage *usage);

/* Generate individual pointer struct classes (__intPtr, etc.) */
void generate_ptr_struct_classes_selective(const PtrUsage *usage);

/* Get pointer class descriptor (e.g., "L__intPtr;") */
const char *ptr_type_descriptor(PtrTypeIndex type);

/* Get pointer class name (e.g., "__intPtr") */
const char *ptr_type_class_name(PtrTypeIndex type);

/* Convert JVM type tag to PtrTypeIndex (e.g., 'B' -> PTR_TYPE_CHAR) */
PtrTypeIndex ptr_type_index_from_jvm_tag(char tag);

/* Get pointer base array descriptor (e.g., "[I") */
const char *ptr_type_base_descriptor(PtrTypeIndex type);

/* Get pointer element descriptor (e.g., "I") */
const char *ptr_type_elem_descriptor(PtrTypeIndex type);

/* Get array load opcode (iaload, baload, etc.) */
uint8_t ptr_type_aload_opcode(PtrTypeIndex type);

/* Get array store opcode (iastore, bastore, etc.) */
uint8_t ptr_type_astore_opcode(PtrTypeIndex type);

/* Check if pointer element is wide (long/double = 2 slots) */
bool ptr_type_is_wide(PtrTypeIndex type);
