#pragma once

#include "cminor_base.h"
#include "classfile.h"

typedef struct FunctionDeclaration_tag FunctionDeclaration;
typedef struct TypeSpecifier_tag TypeSpecifier;

/*
 * Constant Pool Entry Types (for codegen)
 */
typedef enum
{
    CP_CONST_INT = 1,
    CP_CONST_LONG,
    CP_CONST_FLOAT,
    CP_CONST_DOUBLE,
    CP_CONST_STRING,
    CP_CONST_CLASS,
    CP_CONST_METHOD,
    CP_CONST_FIELD
} CP_ConstantType;

/*
 * Constant Pool Entry (for codegen)
 */
typedef struct CP_Constant
{
    CP_ConstantType type;
    union
    {
        int c_int;
        long c_long;
        float c_float;
        double c_double;
        CS_String c_string;
        struct
        {
            char *name;
            char *class_name;
            char *descriptor;
            int arg_count;
            bool is_native;
            bool is_external;
            int function_index;
            int max_stack;
            int max_locals;
            FunctionDeclaration *func_decl;
        } c_method;
        struct
        {
            char *class_name;
            char *name;
            char *descriptor;
            int field_index;
            bool is_external;
        } c_field;
        struct
        {
            char *name;
            int class_index;
        } c_class;
    } u;
} CP_Constant;

/*
 * ConstantPoolBuilder - JVM constant pool for a class
 *
 * This structure manages the constant pool entries that are shared
 * across all methods in a class. Each class has exactly one constant pool.
 */
typedef struct ConstantPoolBuilder_tag
{
    CF_ConstantPool *cf_cp; /* Actual JVM constant pool (indices are final) */
    CP_Constant *metadata;  /* Additional metadata (arg_count, is_native, etc.) */
    int metadata_count;
    int metadata_capacity;
} ConstantPoolBuilder;

/* Create a new ConstantPoolBuilder */
ConstantPoolBuilder *cp_builder_create();

/* Destroy a ConstantPoolBuilder */
void cp_builder_destroy(ConstantPoolBuilder *cp);

/* Accessors */
CP_Constant *cp_builder_entries(ConstantPoolBuilder *cp);
int cp_builder_count(ConstantPoolBuilder *cp);

/* Add an integer constant, returns pool index */
int cp_builder_add_int(ConstantPoolBuilder *cp, int value);

/* Add a long constant, returns pool index */
int cp_builder_add_long(ConstantPoolBuilder *cp, long value);

/* Add a float constant, returns pool index */
int cp_builder_add_float(ConstantPoolBuilder *cp, float value);

/* Add a double constant, returns pool index */
int cp_builder_add_double(ConstantPoolBuilder *cp, double value);

/* Add a string constant, returns pool index */
int cp_builder_add_string(ConstantPoolBuilder *cp, const char *value);

/* Add a string constant with explicit length (supports embedded nulls) */
int cp_builder_add_string_len(ConstantPoolBuilder *cp, const char *data, int len);

/* Add an external method reference (for calling Java API, etc.)
 * class_name: Internal class name (e.g., "java/lang/String")
 * method_name: Method name (e.g., "toString")
 * descriptor: Method descriptor with L and ; (e.g., "()Ljava/lang/String;") */
int cp_builder_add_methodref(ConstantPoolBuilder *cp, const char *class_name,
                             const char *method_name, const char *descriptor);

/* Add a method reference with Cminor type info
 * descriptor: Method descriptor (e.g., "(ILjava/lang/String;)V") */
int cp_builder_add_methodref_typed(ConstantPoolBuilder *cp, const char *class_name,
                                   const char *method_name, const char *descriptor,
                                   FunctionDeclaration *func, int arg_count);

/* Add an external field reference (for accessing Java fields)
 * class_name: Internal class name (e.g., "java/lang/System")
 * field_name: Field name (e.g., "out")
 * descriptor: Field descriptor with L and ; (e.g., "Ljava/io/PrintStream;") */
int cp_builder_add_fieldref(ConstantPoolBuilder *cp, const char *class_name,
                            const char *field_name, const char *descriptor);

/* Add a field reference with Cminor type info
 * descriptor: Field descriptor (e.g., "Ljava/lang/String;", "[I") */
int cp_builder_add_fieldref_typed(ConstantPoolBuilder *cp, const char *class_name,
                                  const char *field_name, const char *descriptor,
                                  TypeSpecifier *type_spec);

/* Add a class reference, returns pool index
 * class_name: Internal class name format for CONSTANT_Class_info
 *   - Normal classes: "java/lang/String" (no L and ;)
 *   - Array types: "[I", "[Ljava/lang/Object;" (descriptor format)
 * Use cg_jvm_class_name() to get the correct format from TypeSpecifier */
int cp_builder_add_class(ConstantPoolBuilder *cp, const char *class_name);

/* Low-level API for codegen_constants */
void cp_builder_ensure_capacity(ConstantPoolBuilder *cp, int additional);
int cp_builder_increment_count(ConstantPoolBuilder *cp);

/* Get the underlying CF_ConstantPool (for serialize) */
CF_ConstantPool *cp_builder_get_cf_cp(ConstantPoolBuilder *cp);

/* Take ownership of the underlying CF_ConstantPool (sets internal pointer to NULL) */
CF_ConstantPool *cp_builder_take_cf_cp(ConstantPoolBuilder *cp);
