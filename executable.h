#pragma once

#include "classfile.h"
#include "constant_pool.h"
#include "codegen_types.h"
#include "ast.h"

/*
 * Compiler output format structures
 * These represent the intermediate format between code generation
 * and JVM binary serialization (.jvm files)
 */

typedef enum
{
    CS_FUNC_SIG_FROM_DECL = 0,
    CS_FUNC_SIG_C_MAIN,
    CS_FUNC_SIG_JVM_MAIN_WRAPPER
} CS_FunctionSignatureKind;

typedef struct
{
    char *name;
    FunctionDeclaration *decl;
    CS_FunctionSignatureKind signature_kind;
    int arg_count;
    bool is_native;
    bool is_jvm_main;   /* Function should be emitted as JVM main */
    bool is_static;     /* static function -> private in JVM */
    bool main_has_args; /* main takes (int argc, char *argv[]) */
    uint8_t *code;
    int code_size;
    int max_stack;
    int max_locals;
    int constant_pool_index;
    /* StackMapTable frames for JVM verification */
    CF_StackMapFrame *stack_map_frames;
    int stack_map_frame_count;
    /* LineNumberTable for debugging */
    CF_LineNumberEntry *line_numbers;
    int line_number_count;
} CS_Function;

/* Helper method for split <clinit> */
typedef struct
{
    uint8_t *code;
    int code_size;
    int max_stack;
    int max_locals;
} CS_ClinitPart;

typedef struct
{
    ConstantPoolBuilder *cp; /* Constant pool (owned) */
    CG_StaticField *jvm_static_fields;
    int jvm_static_field_count;
    CG_ClassDef *jvm_class_defs;
    int jvm_class_def_count;
    CS_Function *functions;
    int function_count;

    /* Constant pool used while constructing StackMapTable frames */
    CF_ConstantPool *stackmap_constant_pool;

    /* <clinit> method for static field initialization */
    uint8_t *clinit_code;
    int clinit_code_size;
    int clinit_max_stack;
    int clinit_max_locals;

    /* Split clinit helper methods (when <clinit> exceeds 64KB) */
    CS_ClinitPart *clinit_parts;
    int clinit_part_count;
} CS_Executable;
