/*
 * JVM Type System for Codegen
 *
 * Maps C types to JVM types. This is a codegen-only module.
 */

#include "codegen_jvm_types.h"
#include "cminor_type.h"
#include "synthetic_codegen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward declarations */
static const char *basic_descriptor(CS_BasicType type);

typedef struct
{
    char *descriptor;
    CG_JVMRefKind ref_kind;
    int pointer_depth;
} JVMDescriptorResult;

static JVMDescriptorResult build_jvm_descriptor(TypeSpecifier *type);

/* ============================================================
 * Basic Type Descriptors
 * ============================================================ */

static const char *basic_descriptor(CS_BasicType type)
{
    switch (type)
    {
    case CS_VOID_TYPE:
        return "V";
    case CS_CHAR_TYPE:
        return "B"; /* Java byte for C char */
    case CS_SHORT_TYPE:
        return "S"; /* Java short */
    case CS_BOOLEAN_TYPE:
        return "Z"; /* Java boolean */
    case CS_INT_TYPE:
        return "I"; /* Java int */
    case CS_LONG_TYPE:
        return "J"; /* Java long */
    case CS_FLOAT_TYPE:
        return "F"; /* Java float */
    case CS_DOUBLE_TYPE:
        return "D"; /* Java double */
    default:
        fprintf(stderr, "basic_descriptor: unknown CS_BasicType %d\n", type);
        exit(1);
    }
}

/* ============================================================
 * Pointer Runtime Kind Selection
 * ============================================================ */

static CG_PointerRuntimeKind pointer_runtime_kind_from_element(TypeSpecifier *element)
{
    if (!element)
    {
        fprintf(stderr, "pointer_runtime_kind_from_element: element is NULL\n");
        exit(1);
    }

    if (cs_type_is_pointer(element) || cs_type_is_array(element) ||
        cs_type_is_void(element) ||
        (cs_type_is_named(element) && cs_type_is_basic_struct_or_union(element)) ||
        cs_type_is_basic_struct_or_union(element))
    {
        return CG_PTR_RUNTIME_OBJECT;
    }

    if (cs_type_is_enum(element))
    {
        return CG_PTR_RUNTIME_INT;
    }

    if (cs_type_is_char_exact(element))
    {
        return CG_PTR_RUNTIME_CHAR;
    }
    if (cs_type_is_bool(element))
    {
        return CG_PTR_RUNTIME_BOOL;
    }
    if (cs_type_is_short_exact(element))
    {
        return CG_PTR_RUNTIME_SHORT;
    }
    if (cs_type_is_long_exact(element))
    {
        return CG_PTR_RUNTIME_LONG;
    }
    if (cs_type_is_float_exact(element))
    {
        return CG_PTR_RUNTIME_FLOAT;
    }
    if (cs_type_is_double_exact(element))
    {
        return CG_PTR_RUNTIME_DOUBLE;
    }

    if (cs_type_is_int_exact(element))
    {
        return CG_PTR_RUNTIME_INT;
    }

    fprintf(stderr, "pointer_runtime_kind_from_element: unsupported element type\n");
    exit(1);
}

/* ============================================================
 * Object Descriptor Generation
 * ============================================================ */

/* Generate internal class name (for CONSTANT_Class_info)
 * Returns: "java/lang/String" (no L and ;) */
static const char *object_internal_name(TypeSpecifier *type)
{
    const char *name = cs_type_user_type_name(type);
    if (name && name[0])
    {
        return name; /* Already in internal format */
    }
    fprintf(stderr, "object_internal_name: type has no user_type_name\n");
    exit(1);
}

/* Generate field/method descriptor (for signatures)
 * Returns: "Ljava/lang/String;" (with L and ;) */
static char *object_descriptor(TypeSpecifier *type)
{
    const char *name = cs_type_user_type_name(type);
    if (name && name[0])
    {
        int len = strlen(name);
        char *descriptor = (char *)calloc(len + 3, sizeof(char));
        descriptor[0] = 'L';
        strncpy(descriptor + 1, name, len);
        descriptor[len + 1] = ';';
        descriptor[len + 2] = '\0';
        return descriptor;
    }
    fprintf(stderr, "object_descriptor: type has no user_type_name\n");
    exit(1);
}

/* ============================================================
 * JVM Descriptor Building
 * ============================================================ */

static JVMDescriptorResult build_jvm_descriptor(TypeSpecifier *type)
{
    JVMDescriptorResult result = {};
    result.descriptor = NULL;
    result.ref_kind = CG_JVM_REF_INVALID;
    result.pointer_depth = 0;

    if (!type)
    {
        fprintf(stderr, "build_jvm_descriptor: type is NULL\n");
        exit(1);
    }

    CS_TypeKind kind = cs_type_kind(type);

    switch (kind)
    {
    case CS_TYPE_POINTER:
    {
        /* Check for void* - treat as generic Object reference */
        if (cs_type_is_void(cs_type_child(type)))
        {
            result.ref_kind = CG_JVM_REF_OBJECT;
            result.pointer_depth = 1;
            result.descriptor = strdup("Ljava/lang/Object;");
            return result;
        }

        /* Pointer -> runtime pointer class */
        JVMDescriptorResult child = build_jvm_descriptor(cs_type_child(type));
        PtrTypeIndex ptr_kind = (PtrTypeIndex)pointer_runtime_kind_from_element(
            cs_type_child(type));
        const char *runtime_name = ptr_type_class_name(ptr_kind);
        int len = strlen(runtime_name);
        char *descriptor = (char *)calloc(len + 3, sizeof(char));
        descriptor[0] = 'L';
        strncpy(descriptor + 1, runtime_name, len);
        descriptor[len + 1] = ';';
        descriptor[len + 2] = '\0';
        result.ref_kind = CG_JVM_REF_POINTER;
        result.pointer_depth = child.pointer_depth + 1;
        result.descriptor = descriptor;
        free(child.descriptor);
        return result;
    }
    case CS_TYPE_ARRAY:
    {
        JVMDescriptorResult child = build_jvm_descriptor(cs_type_child(type));
        int len = strlen(child.descriptor);
        char *descriptor = (char *)calloc(len + 2, sizeof(char));
        descriptor[0] = '[';
        strcpy(descriptor + 1, child.descriptor);
        result.ref_kind = CG_JVM_REF_ARRAY;
        result.pointer_depth = child.pointer_depth;
        result.descriptor = descriptor;
        free(child.descriptor);
        return result;
    }
    case CS_TYPE_NAMED:
    {
        /* Named enum is treated as int primitive */
        CS_BasicType named_basic = cs_type_basic_type(type);
        if (named_basic == CS_ENUM_TYPE)
        {
            result.ref_kind = CG_JVM_REF_PRIMITIVE;
            result.pointer_depth = 0;
            result.descriptor = strdup("I");
            return result;
        }
        /* Named struct/union are objects */
        result.ref_kind = CG_JVM_REF_OBJECT;
        result.pointer_depth = 0;
        result.descriptor = object_descriptor(type);
        return result;
    }
    case CS_TYPE_BASIC:
    default:
    {
        CS_BasicType basic = cs_type_basic_type(type);
        switch (basic)
        {
        case CS_STRUCT_TYPE:
        case CS_UNION_TYPE:
            result.ref_kind = CG_JVM_REF_OBJECT;
            result.pointer_depth = 0;
            result.descriptor = object_descriptor(type);
            return result;
        case CS_TYPEDEF_NAME:
            /* Typedef should be resolved before codegen.
             * If we reach here, it's a bug in the compiler. */
            fprintf(stderr, "Error: unresolved typedef '%s' in codegen\n",
                    cs_type_user_type_name(type) ? cs_type_user_type_name(type) : "<unknown>");
            exit(1);
        default:
            result.ref_kind = CG_JVM_REF_PRIMITIVE;
            result.pointer_depth = 0;
            result.descriptor = strdup(basic_descriptor(basic));
            return result;
        }
    }
    }
}

/* ============================================================
 * Public API
 * ============================================================ */

CG_JVMTypeInfo cg_jvm_type_info(TypeSpecifier *type)
{
    CG_JVMTypeInfo info;
    info.descriptor = NULL;
    info.ref_kind = CG_JVM_REF_INVALID;
    info.pointer_depth = 0;

    if (!type)
    {
        fprintf(stderr, "cg_jvm_type_info: type is NULL\n");
        exit(1);
    }

    JVMDescriptorResult result = build_jvm_descriptor(type);

    info.descriptor = result.descriptor;
    info.ref_kind = result.ref_kind;
    info.pointer_depth = result.pointer_depth;

    return info;
}

const char *cg_jvm_descriptor(TypeSpecifier *type)
{
    if (!type)
    {
        fprintf(stderr, "cg_jvm_descriptor: type is NULL\n");
        exit(1);
    }

    CG_JVMTypeInfo info = cg_jvm_type_info(type);
    return info.descriptor;
}

const char *cg_jvm_class_name(TypeSpecifier *type)
{
    if (!type)
    {
        fprintf(stderr, "cg_jvm_class_name: type is NULL\n");
        exit(1);
    }

    CG_JVMTypeInfo info = cg_jvm_type_info(type);

    switch (info.ref_kind)
    {
    case CG_JVM_REF_ARRAY:
        /* Arrays: descriptor is already correct for CONSTANT_Class_info
         * e.g., "[I", "[Ljava/lang/Object;" */
        return info.descriptor;

    case CG_JVM_REF_POINTER:
        /* Pointer wrapper classes: get internal class name directly
         * Returns "__intPtr", "__charPtr", etc. (no L and ;) */
        {
            CG_PointerRuntimeKind kind = cg_pointer_runtime_kind(type);
            return ptr_type_class_name((PtrTypeIndex)kind);
        }

    case CG_JVM_REF_OBJECT:
        /* Object types: get internal class name directly
         * Returns "java/lang/String" (no L and ;) */
        return object_internal_name(type);

    case CG_JVM_REF_PRIMITIVE:
        /* Primitives: single char descriptor ("I", "J", etc.)
         * Not normally used in CONSTANT_Class_info, but return as-is */
        return info.descriptor;

    default:
        fprintf(stderr, "cg_jvm_class_name: unknown ref_kind %d\n", info.ref_kind);
        exit(1);
    }
}

CG_JVMRefKind cg_jvm_ref_kind(TypeSpecifier *type)
{
    if (!type)
    {
        fprintf(stderr, "cg_jvm_ref_kind: type is NULL\n");
        exit(1);
    }

    CG_JVMTypeInfo info = cg_jvm_type_info(type);
    return info.ref_kind;
}

int cg_jvm_pointer_depth(TypeSpecifier *type)
{
    if (!type)
    {
        fprintf(stderr, "cg_jvm_pointer_depth: type is NULL\n");
        exit(1);
    }

    CG_JVMTypeInfo info = cg_jvm_type_info(type);
    return (int)info.pointer_depth;
}

bool cg_jvm_is_reference(CG_JVMRefKind kind)
{
    return kind == CG_JVM_REF_POINTER || kind == CG_JVM_REF_ARRAY ||
           kind == CG_JVM_REF_OBJECT;
}

const char *cg_jvm_pointer_element_descriptor(TypeSpecifier *type)
{
    if (!type || !cs_type_is_pointer(type) || !cs_type_child(type))
    {
        fprintf(stderr, "cg_jvm_pointer_element_descriptor: invalid pointer type\n");
        exit(1);
    }

    const char *desc = cg_jvm_descriptor(cs_type_child(type));
    if (!desc)
    {
        fprintf(stderr, "cg_jvm_pointer_element_descriptor: failed to get descriptor\n");
        exit(1);
    }
    return desc;
}

const char *cg_jvm_pointer_base_array_descriptor(TypeSpecifier *type)
{
    /* Return descriptor for the base array of a pointer type.
     * For int* this returns "[I", for char* returns "[B", etc.
     * For struct*, void*, T** (object pointers) this returns "[Ljava/lang/Object;"
     * because __objectPtr.base is always Object[], not a specifically-typed array.
     * This is used for __ptr_create which takes (base_array, offset). */
    if (!type || !cs_type_is_pointer(type) || !cs_type_child(type))
    {
        fprintf(stderr, "cg_jvm_pointer_base_array_descriptor: invalid pointer type\n");
        exit(1);
    }

    /* Check if this is an object pointer type (struct*, void*, T**, etc.)
     * These all use __objectPtr which has base field of type Object[] */
    CG_PointerRuntimeKind kind = cg_pointer_runtime_kind(type);
    if (kind == CG_PTR_RUNTIME_OBJECT)
    {
        return "[Ljava/lang/Object;";
    }

    const char *elem_desc = cg_jvm_descriptor(cs_type_child(type));
    if (!elem_desc)
    {
        fprintf(stderr, "cg_jvm_pointer_base_array_descriptor: failed to get element descriptor\n");
        exit(1);
    }

    /* Allocate and build array descriptor */
    int len = strlen(elem_desc);
    char *desc = (char *)calloc(len + 2, sizeof(char));
    desc[0] = '[';
    strcpy(desc + 1, elem_desc);

    return desc;
}

/* ============================================================
 * Pointer Runtime Helpers
 * ============================================================ */

CG_PointerRuntimeKind cg_pointer_runtime_kind(TypeSpecifier *type)
{
    TypeSpecifier *element = type;
    if (type && cs_type_is_pointer(type))
    {
        element = cs_type_child(type);
    }
    return pointer_runtime_kind_from_element(element);
}

const char *cg_pointer_runtime_suffix(TypeSpecifier *type)
{
    CG_PointerRuntimeKind kind = cg_pointer_runtime_kind(type);
    switch (kind)
    {
    case CG_PTR_RUNTIME_CHAR:
        return "_char";
    case CG_PTR_RUNTIME_BOOL:
        return "_bool";
    case CG_PTR_RUNTIME_SHORT:
        return "_short";
    case CG_PTR_RUNTIME_INT:
        return "_int";
    case CG_PTR_RUNTIME_LONG:
        return "_long";
    case CG_PTR_RUNTIME_FLOAT:
        return "_float";
    case CG_PTR_RUNTIME_DOUBLE:
        return "_double";
    case CG_PTR_RUNTIME_OBJECT:
        return "_object";
    default:
        return "_int";
    }
}

const char *cg_heap_lift_array_descriptor(TypeSpecifier *type)
{
    /* For references (pointers, arrays, structs), use Object[] */
    if (cs_type_is_pointer(type) || cs_type_is_array(type) ||
        cs_type_is_basic_struct_or_union(type))
    {
        return "[Ljava/lang/Object;";
    }

    /* For primitives, use the appropriate primitive array */
    if (cs_type_is_char_exact(type))
    {
        return "[B";
    }
    if (cs_type_is_bool(type))
    {
        return "[Z";
    }
    if (cs_type_is_short_exact(type))
    {
        return "[S";
    }
    if (cs_type_is_long_exact(type))
    {
        return "[J";
    }
    if (cs_type_is_float_exact(type))
    {
        return "[F";
    }
    if (cs_type_is_double_exact(type))
    {
        return "[D";
    }

    /* Default: int (also handles enum) */
    return "[I";
}

/* ============================================================
 * Method Descriptor Generation
 * ============================================================ */

typedef struct
{
    FunctionDeclaration *func;
    char *descriptor;
} CG_MethodDescriptorEntry;

static CG_MethodDescriptorEntry *method_descriptor_cache = NULL;
static int method_descriptor_cache_count = 0;
static int method_descriptor_cache_capacity = 0;

static const char *cg_cached_method_descriptor(FunctionDeclaration *func)
{
    if (!func)
    {
        return NULL;
    }

    for (int i = 0; i < method_descriptor_cache_count; ++i)
    {
        if (method_descriptor_cache[i].func == func)
        {
            return method_descriptor_cache[i].descriptor;
        }
    }

    return NULL;
}

static const char *cg_store_method_descriptor(FunctionDeclaration *func, char *descriptor)
{
    if (!func || !descriptor)
    {
        return descriptor;
    }

    if (method_descriptor_cache_count == method_descriptor_cache_capacity)
    {
        int next_capacity = method_descriptor_cache_capacity == 0 ? 16 : method_descriptor_cache_capacity * 2;
        CG_MethodDescriptorEntry *next_cache =
            (CG_MethodDescriptorEntry *)calloc(next_capacity, sizeof(CG_MethodDescriptorEntry));
        for (int i = 0; i < method_descriptor_cache_count; ++i)
        {
            next_cache[i] = method_descriptor_cache[i];
        }
        if (method_descriptor_cache)
        {
            free(method_descriptor_cache);
        }
        method_descriptor_cache = next_cache;
        method_descriptor_cache_capacity = next_capacity;
    }

    method_descriptor_cache[method_descriptor_cache_count].func = func;
    method_descriptor_cache[method_descriptor_cache_count].descriptor = descriptor;
    method_descriptor_cache_count += 1;

    return descriptor;
}

const char *cg_jvm_method_descriptor(FunctionDeclaration *func)
{
    if (!func)
    {
        return "()V";
    }

    const char *cached = cg_cached_method_descriptor(func);
    if (cached)
    {
        return cached;
    }

    /* Build descriptor: (param_types)return_type */
    char buffer[1024];
    int pos = 0;
    buffer[pos++] = '(';

    /* Add parameter type descriptors */
    for (ParameterList *p = func->param; p; p = p->next)
    {
        if (p->is_ellipsis)
        {
            continue; /* Skip ellipsis */
        }
        const char *desc = cg_jvm_descriptor(p->type);
        if (desc)
        {
            int len = strlen(desc);
            if (pos + len < sizeof buffer - 2)
            {
                memcpy(&buffer[pos], desc, len);
                pos += len;
            }
        }
    }

    /* Add varargs parameter for variadic functions: Object[] */
    if (func->is_variadic)
    {
        const char *varargs_desc = "[Ljava/lang/Object;";
        int len = strlen(varargs_desc);
        if (pos + len < sizeof buffer - 2)
        {
            memcpy(&buffer[pos], varargs_desc, len);
            pos += len;
        }
    }

    buffer[pos++] = ')';

    /* Add return type descriptor */
    const char *ret_desc = cg_jvm_descriptor(func->type);
    if (ret_desc)
    {
        int len = strlen(ret_desc);
        if (pos + len < sizeof buffer - 1)
        {
            memcpy(&buffer[pos], ret_desc, len);
            pos += len;
        }
    }
    else
    {
        buffer[pos++] = 'V';
    }

    buffer[pos] = '\0';
    return cg_store_method_descriptor(func, strdup(buffer));
}

/* ============================================================
 * JVM Value Tag (for instruction selection)
 * ============================================================ */

CF_ValueTag cg_to_value_tag(TypeSpecifier *type)
{
    if (!type)
    {
        fprintf(stderr, "cg_to_value_tag: type is NULL\n");
        exit(1);
    }

    /* Enum types are always int (ordinal value) */
    if (cs_type_is_enum(type))
    {
        return CF_VAL_INT;
    }

    /* Check exact types for proper JVM value tag selection.
     * JVM has distinct instructions for int, long, float, double. */
    if (cs_type_is_long_exact(type))
    {
        return CF_VAL_LONG;
    }
    if (cs_type_is_float_exact(type))
    {
        return CF_VAL_FLOAT;
    }
    if (cs_type_is_double_exact(type))
    {
        return CF_VAL_DOUBLE;
    }
    /* char, short, int, boolean all use JVM int instructions */
    if (cs_type_is_char_exact(type) || cs_type_is_short_exact(type) ||
        cs_type_is_int_exact(type) || cs_type_is_bool(type))
    {
        return CF_VAL_INT;
    }

    /* All composite types (arrays, pointers, structs) are objects */
    return CF_VAL_OBJECT;
}

CF_ValueTag cg_decl_value_tag(Declaration *decl)
{
    if (!decl)
    {
        fprintf(stderr, "cg_decl_value_tag: decl is NULL\n");
        exit(1);
    }

    /* Heap-lifted variables are stored as array references on JVM.
     * Even if the C type is int, a heap-lifted int is stored as int[1]. */
    if (decl->needs_heap_lift)
    {
        return CF_VAL_OBJECT;
    }

    return cg_to_value_tag(decl->type);
}

CF_ValueTag cg_array_element_value_tag(TypeSpecifier *array_type)
{
    if (!array_type || !cs_type_is_array(array_type))
    {
        fprintf(stderr, "cg_array_element_value_tag: type is not an array\n");
        exit(1);
    }

    /* Use the direct child element type (one level only).
     * Example: int[][] -> int[], double[] -> double */
    TypeSpecifier *elem = cs_type_child(array_type);
    if (!elem)
    {
        fprintf(stderr, "cg_array_element_value_tag: element type is NULL\n");
        exit(1);
    }

    /* Check the element type using API */
    CS_BasicType basic = cs_type_basic_type(elem);
    switch (basic)
    {
    case CS_LONG_TYPE:
        return CF_VAL_LONG;
    case CS_FLOAT_TYPE:
        return CF_VAL_FLOAT;
    case CS_DOUBLE_TYPE:
        return CF_VAL_DOUBLE;
    case CS_CHAR_TYPE:
    case CS_SHORT_TYPE:
    case CS_INT_TYPE:
    case CS_BOOLEAN_TYPE:
    case CS_ENUM_TYPE:
        return CF_VAL_INT;
    default:
        return CF_VAL_OBJECT;
    }
}
