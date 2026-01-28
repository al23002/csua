#pragma once

/*
 * JVM Type System for Codegen
 *
 * This module handles the mapping from C types (TypeSpecifier) to JVM types.
 * It is codegen-only and should not be used by the semantic analyzer.
 *
 * Responsibilities:
 * - JVM descriptor generation from C types
 * - JVM reference kind classification
 * - Pointer depth calculation
 * - All JVM-specific type decisions
 */

#include "ast.h"

/* JVM reference kind classification */
typedef enum
{
    CG_JVM_REF_INVALID = 0,
    CG_JVM_REF_PRIMITIVE, /* int, long, float, double, etc. */
    CG_JVM_REF_OBJECT,    /* struct, typedef'd object */
    CG_JVM_REF_POINTER,   /* C pointer -> runtime pointer class */
    CG_JVM_REF_ARRAY      /* C array -> JVM array */
} CG_JVMRefKind;

/* JVM type information computed from C type */
typedef struct
{
    const char *descriptor; /* JVM type descriptor (e.g., "I", "[I", "L__intPtr;") */
    CG_JVMRefKind ref_kind; /* Classification of the type */
    int pointer_depth;      /* Number of pointer indirections */
} CG_JVMTypeInfo;

/* Compute JVM type information from C type.
 * This is the main entry point for JVM type queries.
 * The returned descriptor is valid for the lifetime of the compilation. */
CG_JVMTypeInfo cg_jvm_type_info(TypeSpecifier *type);

/* Get JVM descriptor for a C type (for field/method signatures)
 * Returns: "Ljava/lang/String;", "[I", "I", etc. */
const char *cg_jvm_descriptor(TypeSpecifier *type);

/* Get JVM class name for CONSTANT_Class_info (for checkcast, new, etc.)
 * Returns: "java/lang/String" (no L;), "[I" (arrays as-is), etc.
 * This converts descriptor format to internal class name format. */
const char *cg_jvm_class_name(TypeSpecifier *type);

/* Get JVM reference kind for a C type */
CG_JVMRefKind cg_jvm_ref_kind(TypeSpecifier *type);

/* Get pointer depth for a C type */
int cg_jvm_pointer_depth(TypeSpecifier *type);

/* Check if a JVM ref kind represents a JVM reference (object/array/pointer) */
bool cg_jvm_is_reference(CG_JVMRefKind kind);

/* Get the descriptor for the pointer's element type */
const char *cg_jvm_pointer_element_descriptor(TypeSpecifier *type);

/* Get the descriptor for the base array of a pointer (e.g., int* -> "[I") */
const char *cg_jvm_pointer_base_array_descriptor(TypeSpecifier *type);

/* Pointer runtime types */
typedef enum
{
    CG_PTR_RUNTIME_CHAR = 0, /* char -> byte[] */
    CG_PTR_RUNTIME_BOOL,     /* _Bool -> boolean[] */
    CG_PTR_RUNTIME_SHORT,    /* short -> short[] */
    CG_PTR_RUNTIME_INT,      /* int -> int[] */
    CG_PTR_RUNTIME_LONG,     /* long -> long[] */
    CG_PTR_RUNTIME_FLOAT,    /* float -> float[] */
    CG_PTR_RUNTIME_DOUBLE,   /* double -> double[] */
    CG_PTR_RUNTIME_OBJECT,   /* void* -> Object[] */
} CG_PointerRuntimeKind;

/* Get the pointer runtime kind for codegen */
CG_PointerRuntimeKind cg_pointer_runtime_kind(TypeSpecifier *type);

/* Get the suffix for pointer runtime helper methods */
const char *cg_pointer_runtime_suffix(TypeSpecifier *type);

/* Get the array descriptor for heap-lifted variables.
 * Primitives: int->"[I", char->"[B", short->"[S", long->"[J", float->"[F", double->"[D", bool->"[Z"
 * References (pointers, arrays, structs): "[Ljava/lang/Object;" */
const char *cg_heap_lift_array_descriptor(TypeSpecifier *type);

/* Generate JVM method descriptor from function declaration */
const char *cg_jvm_method_descriptor(FunctionDeclaration *func);

/* ============================================================
 * JVM Value Tag (for instruction selection)
 * ============================================================ */

#include "classfile.h"

/* Get JVM value tag for a type.
 * Maps C types to JVM operand stack categories:
 * - int, char, short, bool -> CF_VAL_INT
 * - long -> CF_VAL_LONG
 * - float -> CF_VAL_FLOAT
 * - double -> CF_VAL_DOUBLE
 * - arrays, pointers, structs, named enums -> CF_VAL_OBJECT */
CF_ValueTag cg_to_value_tag(TypeSpecifier *type);

/* Get JVM value tag for a declaration, considering heap-lift.
 * If decl->needs_heap_lift is true, returns CF_VAL_OBJECT since
 * the variable is stored as an array reference on the JVM. */
CF_ValueTag cg_decl_value_tag(Declaration *decl);

/* Get JVM value tag for array elements.
 * Requires: cs_type_is_array(array_type) == true
 * int[] -> CF_VAL_INT, double[] -> CF_VAL_DOUBLE
 * struct Foo[] -> CF_VAL_OBJECT */
CF_ValueTag cg_array_element_value_tag(TypeSpecifier *array_type);
