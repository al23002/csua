#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "classfile.h"
#include "constant_pool.h"
#include "ast.h"
#include "compiler.h"
#include "scanner.h" /* For Scanner struct definition (Cminor requires visible struct) */
#include "executable.h"
#include "codebuilder_core.h"
#include "codebuilder_frame.h"
#include "codebuilder_label.h"
#include "codebuilder_part1.h"
#include "codebuilder_part2.h"
#include "codebuilder_part3.h"
#include "codebuilder_stackmap.h"
#include "codebuilder_types.h"
#include "codebuilder_ptr.h"
#include "codegenvisitor.h"
#include "cminor_type.h"
#include "header_store.h"
#include "stackmap.h"
#include "method_code.h"
#include "classfile_opcode.h"
#include "synthetic_codegen.h"
#include "codegen_constants.h"
#include "codegen_jvm_types.h"
#include "codegen_symbols.h"
#include "codegenvisitor_util.h"
#include "util.h"

enum
{
    OUTPUT_PATH_MAX = 4096
};

/* Forward declaration for compile_source_for_codegen (defined in compiler.c) */
bool compile_source_for_codegen(CompilerContext *ctx, const char *path, bool is_embedded);

static void write_u1(FILE *fp, uint8_t v)
{
    char buf[1];
    buf[0] = (char)v;
    fwrite((char *)buf, 1, 1, fp);
}

static void write_u4(FILE *fp, uint32_t v)
{
    char buf[4];
    buf[0] = (char)(v >> 24);
    buf[1] = (char)(v >> 16);
    buf[2] = (char)(v >> 8);
    buf[3] = (char)v;
    fwrite((char *)buf, 1, 4, fp);
}

static void write_u8(FILE *fp, uint64_t v)
{
    write_u4(fp, (uint32_t)(v >> 32));
    write_u4(fp, (uint32_t)(v & 0xffffffff));
}

static void write_bytes(FILE *fp, const char *p, int len)
{
    fwrite(p, 1, len, fp);
}

static void write_string(FILE *fp, const char *s)
{
    uint32_t len = (uint32_t)strlen(s);
    write_u4(fp, len);
    write_bytes(fp, s, len);
}

static uint32_t read_u4_be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint16_t read_u2_be(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static int32_t read_s4_be(const uint8_t *p)
{
    return (int32_t)read_u4_be(p);
}

static int16_t read_s2_be(const uint8_t *p)
{
    return (int16_t)read_u2_be(p);
}

static CS_Function *find_function_entry(CodegenVisitor *cgen,
                                        FunctionDeclaration *func)
{
    if (!func || func->index < 0)
    {
        return NULL;
    }

    for (int i = 0; i < cgen->function_count; ++i)
    {
        if (cgen->functions[i].constant_pool_index == func->index)
        {
            return &cgen->functions[i];
        }
    }

    return NULL;
}

static void finalize_function(CodegenVisitor *cgen, CS_Function *info)
{
    if (!info)
    {
        return;
    }

    info->max_stack = cgen->builder->max_stack;
    info->max_locals = cgen->builder->max_locals;
    MethodCode *mc = code_output_method(cgen->output);
    info->code_size = method_code_size(mc);
    if (info->code_size)
    {
        info->code = (uint8_t *)calloc(info->code_size, sizeof(uint8_t));
        memcpy(info->code, method_code_data(mc), info->code_size);
    }

    /* Copy pre-generated StackMapTable frames (generated in codegen_finish_function) */
    info->stack_map_frames = cgen->temp_stack_map_frames;
    info->stack_map_frame_count = cgen->temp_stack_map_frame_count;
    /* Clear temp storage to prevent double-free */
    cgen->temp_stack_map_frames = NULL;
    cgen->temp_stack_map_frame_count = 0;

    /* Copy LineNumberTable entries (skip entries with line_number <= 0) */
    int lnc = method_code_line_number_count(mc);
    if (lnc > 0)
    {
        LineNumberEntry *src = method_code_line_numbers(mc);
        /* Count valid entries */
        int valid_count = 0;
        for (int i = 0; i < lnc; i++)
        {
            if (src[i].line_number > 0)
            {
                valid_count++;
            }
        }
        if (valid_count > 0)
        {
            info->line_numbers = (CF_LineNumberEntry *)calloc(valid_count, sizeof(CF_LineNumberEntry));
            int j = 0;
            for (int i = 0; i < lnc; i++)
            {
                if (src[i].line_number > 0)
                {
                    info->line_numbers[j].start_pc = src[i].start_pc;
                    info->line_numbers[j].line_number = src[i].line_number;
                    j++;
                }
            }
            info->line_number_count = valid_count;
        }
    }
}

/* Threshold for splitting <clinit> method (60KB, leaving margin for JVM 64KB limit) */
enum
{
    CLINIT_SIZE_THRESHOLD = 60000,
    CLINIT_PART_NAME_MAX = 64
};

/* Forward declarations for split support */
static void save_clinit_part(CodegenVisitor *cgen, CS_Executable *exec);
static int generate_array_init_with_split(CodegenVisitor *cgen, Declaration *decl,
                                          CS_Executable *exec, int start_idx);

/* Generate array initialization with streaming (no stack overflow).
 * Instead of evaluating all elements then storing, this evaluates
 * and stores each element immediately. Stack usage: ~3 slots max. */

/* Original streaming function for non-clinit contexts (unused in split mode) */
static void generate_array_init_streaming(CodegenVisitor *cgen, Declaration *decl)
{
    Expression *init = decl->initializer;
    TypeSpecifier *array_type = decl->type;
    TypeSpecifier *elem_type = cs_type_child(array_type);

    /* Count elements */
    int elem_count = 0;
    for (ExpressionList *p = init->u.initializer_list; p; p = p->next)
        elem_count++;

    /* Get declared length (may be larger than initializer count) */
    int declared_len = array_length_from_type(array_type);
    int array_len = declared_len > 0 ? declared_len : elem_count;

    /* Check if this is a struct array */
    bool is_struct_array = cs_type_is_named(elem_type) &&
                           cs_type_is_basic_struct_or_union(elem_type);
    const char *struct_name = is_struct_array ? cs_type_user_type_name(elem_type) : NULL;

    /* Create array: push length, then newarray/anewarray */
    codebuilder_build_iconst(cgen->builder, array_len);
    if (cs_type_is_pointer(elem_type) || cs_type_is_array(elem_type))
    {
        int array_class_idx = cg_find_or_add_array_class(cgen, elem_type);
        codebuilder_build_anewarray(cgen->builder, array_class_idx);
    }
    else if (is_struct_array)
    {
        int class_idx = find_class_index(cgen, struct_name);
        int const_idx = cg_find_or_add_class(cgen, struct_name, class_idx);
        codebuilder_build_anewarray(cgen->builder, const_idx);
    }
    else
    {
        codebuilder_build_newarray(cgen->builder, newarray_type_code(elem_type));
    }

    /* Store array reference in temp local */
    int array_local = allocate_temp_local_for_tag(cgen, CF_VAL_OBJECT);
    codebuilder_build_astore(cgen->builder, array_local);

    /* Initialize each element: for each value, load array, push index, eval, store */
    int idx = 0;
    for (ExpressionList *p = init->u.initializer_list; p; p = p->next, idx++)
    {
        /* Load array reference */
        codebuilder_build_aload(cgen->builder, array_local);
        /* Push index */
        codebuilder_build_iconst(cgen->builder, idx);
        /* Evaluate element expression */
        codegen_traverse_expr(p->expression, cgen);

        /* For struct arrays with nested initializer, create struct object from values */
        if (is_struct_array && p->expression &&
            p->expression->kind == INITIALIZER_LIST_EXPRESSION)
        {
            /* Count field values in this initializer */
            int field_count = 0;
            for (ExpressionList *fp = p->expression->u.initializer_list; fp; fp = fp->next)
                field_count++;

            /* Extract type info for each field value for array-to-pointer conversion */
            TypeSpecifier **value_types = NULL;
            if (field_count > 0)
            {
                value_types = (TypeSpecifier **)calloc(field_count, sizeof(TypeSpecifier *));
                int vi = 0;
                for (ExpressionList *fp = p->expression->u.initializer_list; fp; fp = fp->next, vi++)
                {
                    if (fp->expression)
                        value_types[vi] = fp->expression->type;
                }
            }

            /* Create struct from values on stack */
            cg_emit_struct_from_init_values(cgen, struct_name, NULL, field_count, value_types);

            if (value_types)
                free(value_types);
        }

        /* Store in array using appropriate instruction */
        cg_emit_array_store_for_type(cgen, elem_type);
    }

    /* Load array reference for putstatic */
    codebuilder_build_aload(cgen->builder, array_local);
}

/* Generate <clinit> bytecode using visitor for static field initialization.
 * This uses codegen_traverse_expr() to handle complex initializers like arrays
 * and struct initializer lists. Also calls functions with [[cminor::clinit]]. */

/* Generate clinit helper method name: "clinit$partN" */
static char *make_clinit_part_name(int part_index)
{
    char *name = (char *)calloc(CLINIT_PART_NAME_MAX, sizeof(char));
    snprintf(name, CLINIT_PART_NAME_MAX, "clinit$part%d", part_index);
    return name;
}

/* Save current clinit code as a helper part and reset for next part */
static void save_clinit_part(CodegenVisitor *cgen, CS_Executable *exec)
{
    codebuilder_build_return(cgen->builder);
    MethodCode *mc = code_output_method(cgen->output);
    int code_size = method_code_size(mc);

    if (code_size == 0)
        return;

    /* Expand parts array (manual realloc since realloc is prohibited) */
    int idx = exec->clinit_part_count;
    int new_count = idx + 1;
    CS_ClinitPart *new_parts = (CS_ClinitPart *)calloc(new_count, sizeof(CS_ClinitPart));
    for (int i = 0; i < idx; i++)
    {
        new_parts[i] = exec->clinit_parts[i];
    }
    exec->clinit_parts = new_parts;
    exec->clinit_part_count = new_count;

    CS_ClinitPart *part = &exec->clinit_parts[idx];
    part->code_size = code_size;
    part->code = (uint8_t *)calloc(code_size, sizeof(uint8_t));
    memcpy(part->code, method_code_data(mc), code_size);
    part->max_stack = cgen->builder->max_stack;
    part->max_locals = cgen->builder->max_locals;

    /* Reset for next part */
    code_output_reset_method(cgen->output);
    codegen_begin_function(cgen, NULL);
}

/* Generate array initialization, returning the starting index for continuation.
 * If splitting is needed, saves current part and continues from start_idx.
 * Returns -1 when all elements are initialized. */
static int generate_array_init_with_split(CodegenVisitor *cgen, Declaration *decl,
                                          CS_Executable *exec, int start_idx)
{
    Expression *init = decl->initializer;
    TypeSpecifier *array_type = decl->type;
    TypeSpecifier *elem_type = cs_type_child(array_type);

    /* Count elements */
    int elem_count = 0;
    for (ExpressionList *p = init->u.initializer_list; p; p = p->next)
        elem_count++;

    /* Get declared length (may be larger than initializer count) */
    int declared_len = array_length_from_type(array_type);
    int array_len = declared_len > 0 ? declared_len : elem_count;

    /* Check if this is a struct array */
    bool is_struct_array = cs_type_is_named(elem_type) &&
                           cs_type_is_basic_struct_or_union(elem_type);
    const char *struct_name = is_struct_array ? cs_type_user_type_name(elem_type) : NULL;

    int field_idx = cg_find_or_add_field(cgen, decl);

    if (start_idx == 0)
    {
        /* First call: create array and store in static field */
        codebuilder_build_iconst(cgen->builder, array_len);
        if (cs_type_is_pointer(elem_type) || cs_type_is_array(elem_type))
        {
            int array_class_idx = cg_find_or_add_array_class(cgen, elem_type);
            codebuilder_build_anewarray(cgen->builder, array_class_idx);
        }
        else if (is_struct_array)
        {
            int class_idx = find_class_index(cgen, struct_name);
            int const_idx = cg_find_or_add_class(cgen, struct_name, class_idx);
            codebuilder_build_anewarray(cgen->builder, const_idx);
        }
        else
        {
            codebuilder_build_newarray(cgen->builder, newarray_type_code(elem_type));
        }
        /* Store array in static field immediately */
        codebuilder_build_putstatic(cgen->builder, field_idx);
    }

    /* Skip to start_idx in the list */
    ExpressionList *p = init->u.initializer_list;
    for (int i = 0; i < start_idx && p; i++, p = p->next)
        ;

    /* Initialize elements starting from start_idx */
    int idx = start_idx;
    for (; p; p = p->next, idx++)
    {
        /* Check if we need to split (every 100 elements to avoid too frequent checks) */
        if (idx > start_idx && (idx - start_idx) % 100 == 0)
        {
            MethodCode *mc_check = code_output_method(cgen->output);
            int current_size = method_code_size(mc_check);
            if (current_size > CLINIT_SIZE_THRESHOLD)
            {
                /* Need to split - save current part and return continuation index */
                save_clinit_part(cgen, exec);
                return idx;
            }
        }

        /* Load array from static field */
        codebuilder_build_getstatic(cgen->builder, field_idx);
        /* Push index */
        codebuilder_build_iconst(cgen->builder, idx);
        /* Evaluate element expression */
        codegen_traverse_expr(p->expression, cgen);

        /* For struct arrays with nested initializer, create struct object from values */
        if (is_struct_array && p->expression &&
            p->expression->kind == INITIALIZER_LIST_EXPRESSION)
        {
            /* Count field values in this initializer */
            int field_count = 0;
            for (ExpressionList *fp = p->expression->u.initializer_list; fp; fp = fp->next)
                field_count++;

            /* Extract type info for each field value for array-to-pointer conversion */
            TypeSpecifier **value_types = NULL;
            if (field_count > 0)
            {
                value_types = (TypeSpecifier **)calloc(field_count, sizeof(TypeSpecifier *));
                int vi = 0;
                for (ExpressionList *fp = p->expression->u.initializer_list; fp; fp = fp->next, vi++)
                {
                    if (fp->expression)
                        value_types[vi] = fp->expression->type;
                }
            }

            /* Create struct from values on stack */
            cg_emit_struct_from_init_values(cgen, struct_name, NULL, field_count, value_types);

            if (value_types)
                free(value_types);
        }

        /* Store in array using appropriate instruction */
        cg_emit_array_store_for_type(cgen, elem_type);
    }

    /* All elements initialized */
    return -1;
}

static void generate_clinit_code(CodegenVisitor *cgen, CS_Executable *exec)
{
    /* Initialize parts */
    exec->clinit_parts = NULL;
    exec->clinit_part_count = 0;

    /* Check if any static field needs initialization (has initializer or is struct type) */
    bool needs_clinit = false;
    for (int i = 0; i < cgen->static_field_count; i++)
    {
        Declaration *decl = (Declaration *)cgen->static_fields[i].decl;
        if (decl && decl->initializer)
        {
            needs_clinit = true;
            break;
        }
        /* Struct types need initialization even without explicit initializer */
        if (decl && cs_type_is_named(decl->type) && cs_type_is_basic_struct_or_union(decl->type))
        {
            needs_clinit = true;
            break;
        }
    }

    /* Check if any function has cminor::clinit attribute */
    FileDecl *file_decl = cgen->compiler->current_file_decl;
    if (file_decl)
    {
        for (FunctionDeclarationList *fl = file_decl->functions; fl; fl = fl->next)
        {
            FunctionDeclaration *f = fl->func;
            if (f && find_attribute(f->attributes, CS_ATTRIBUTE_CLINIT))
            {
                needs_clinit = true;
                break;
            }
        }
    }

    if (!needs_clinit)
    {
        exec->clinit_code = NULL;
        exec->clinit_code_size = 0;
        return;
    }

    /* Reset method output and initialize visitor context for <clinit> */
    code_output_reset_method(cgen->output);
    codegen_begin_function(cgen, NULL);

    /* Generate initialization code for each static field */
    for (int i = 0; i < cgen->static_field_count; i++)
    {
        Declaration *decl = (Declaration *)cgen->static_fields[i].decl;
        if (!decl)
            continue;

        /* Check if this is a struct type that needs initialization */
        const char *struct_name = cs_type_user_type_name(decl->type);
        bool is_struct_type = struct_name &&
                              cs_type_is_named(decl->type) &&
                              cs_type_is_basic_struct_or_union(decl->type);

        /* Skip non-struct fields without initializer */
        if (!decl->initializer && !is_struct_type)
            continue;

        /* Array initializer with split support - handle separately (no block scope) */
        if (cs_type_is_array(decl->type) &&
            decl->initializer && decl->initializer->kind == INITIALIZER_LIST_EXPRESSION)
        {
            /* Check if we need to split before this field (code size approaching limit) */
            MethodCode *mc_check = code_output_method(cgen->output);
            int current_size = method_code_size(mc_check);
            if (current_size > CLINIT_SIZE_THRESHOLD)
            {
                /* Save current code as a part and start a new one */
                save_clinit_part(cgen, exec);
            }

            /* Array initializer list: use streaming with split support.
             * This evaluates and stores each element immediately, and splits
             * if code size exceeds threshold. No block scope needed since
             * array is stored directly to static field. */
            int start_idx = 0;
            int next_idx;
            while ((next_idx = generate_array_init_with_split(cgen, decl, exec, start_idx)) >= 0)
            {
                /* Continue from where we left off after split */
                start_idx = next_idx;
            }
            /* Array is already stored in static field by generate_array_init_with_split */
            continue;
        }

        /* Check if we need to split before this field (code size approaching limit) */
        MethodCode *mc_check = code_output_method(cgen->output);
        int current_size = method_code_size(mc_check);
        if (current_size > CLINIT_SIZE_THRESHOLD)
        {
            /* Save current code as a part and start a new one */
            save_clinit_part(cgen, exec);
        }

        /* Begin block scope so temp locals can be reused for each field init */
        codebuilder_begin_block(cgen->builder);

        if (is_struct_type && !decl->initializer)
        {
            /* Struct without initializer: create empty struct with recursive embedded init */
            cg_emit_struct_from_init_values(cgen, struct_name, NULL, 0, NULL);
            /* Stack: [struct_ref] */
            int pool_idx = cg_find_or_add_field(cgen, decl);
            codebuilder_build_putstatic(cgen->builder, pool_idx);
        }
        else if (is_struct_type && decl->initializer->kind == INITIALIZER_LIST_EXPRESSION)
        {
            /* Struct initializer list: evaluate values, create struct, assign fields */
            codegen_traverse_expr(decl->initializer, cgen);
            ExpressionList *init_list = decl->initializer->u.initializer_list;
            int value_count = 0;
            for (ExpressionList *p = init_list; p; p = p->next)
                value_count++;

            /* Extract type info for each field value for array-to-pointer conversion */
            TypeSpecifier **value_types = NULL;
            if (value_count > 0)
            {
                value_types = (TypeSpecifier **)calloc(value_count, sizeof(TypeSpecifier *));
                int vi = 0;
                for (ExpressionList *p = init_list; p; p = p->next, vi++)
                {
                    if (p->expression)
                        value_types[vi] = p->expression->type;
                }
            }

            cg_emit_struct_from_init_values(cgen, struct_name, NULL, value_count, value_types);

            if (value_types)
                free(value_types);
            /* Stack: [struct_ref] */
            int pool_idx = cg_find_or_add_field(cgen, decl);
            codebuilder_build_putstatic(cgen->builder, pool_idx);
        }
        else if (decl->initializer)
        {
            /* Use visitor to evaluate the initializer expression.
             * This handles scalar types and other expressions. */
            codegen_traverse_expr(decl->initializer, cgen);

            /* Generate putstatic to store the value in the static field */
            int pool_idx = cg_find_or_add_field(cgen, decl);
            codebuilder_build_putstatic(cgen->builder, pool_idx);
        }

        /* End block scope - temp locals can be reused for next field init */
        codebuilder_end_block(cgen->builder);
    }

    /* Call functions with cminor::clinit attribute */
    if (file_decl)
    {
        for (FunctionDeclarationList *fl = file_decl->functions; fl; fl = fl->next)
        {
            FunctionDeclaration *f = fl->func;
            if (!f || !find_attribute(f->attributes, CS_ATTRIBUTE_CLINIT))
                continue;

            /* Generate invokestatic for the clinit function */
            int argc = cs_count_parameters(f->param);
            if (f->is_variadic)
            {
                argc += 1;
            }
            int pool_idx = cp_builder_add_methodref_typed(code_output_cp(cgen->output),
                                                          cgen->current_class_name,
                                                          f->name,
                                                          cg_function_descriptor(f),
                                                          f, argc);
            codebuilder_build_invokestatic(cgen->builder, pool_idx);
        }
    }

    /* Check if we have split parts */
    MethodCode *mc_final = code_output_method(cgen->output);
    int final_size = method_code_size(mc_final);

    if (exec->clinit_part_count > 0)
    {
        /* Save the remaining code as the last part */
        if (final_size > 0)
        {
            save_clinit_part(cgen, exec);
        }

        /* Now generate the main <clinit> that just calls all parts */
        code_output_reset_method(cgen->output);
        codegen_begin_function(cgen, NULL);

        for (int p = 0; p < exec->clinit_part_count; p++)
        {
            /* Generate method name: clinit$part0, clinit$part1, ... */
            char *part_name = make_clinit_part_name(p);

            int pool_idx = cp_builder_add_methodref(code_output_cp(cgen->output),
                                                    cgen->current_class_name,
                                                    part_name,
                                                    "()V");
            codebuilder_build_invokestatic(cgen->builder, pool_idx);
        }

        codebuilder_build_return(cgen->builder);

        MethodCode *mc = code_output_method(cgen->output);
        exec->clinit_code_size = method_code_size(mc);
        if (exec->clinit_code_size > 0)
        {
            exec->clinit_code = (uint8_t *)calloc(exec->clinit_code_size, sizeof(uint8_t));
            memcpy(exec->clinit_code, method_code_data(mc), exec->clinit_code_size);
        }
        exec->clinit_max_stack = cgen->builder->max_stack;
        exec->clinit_max_locals = cgen->builder->max_locals;
    }
    else
    {
        /* No split needed, just add return and copy */
        codebuilder_build_return(cgen->builder);

        MethodCode *mc = code_output_method(cgen->output);
        exec->clinit_code_size = method_code_size(mc);
        if (exec->clinit_code_size > 0)
        {
            exec->clinit_code = (uint8_t *)calloc(exec->clinit_code_size, sizeof(uint8_t));
            memcpy(exec->clinit_code, method_code_data(mc), exec->clinit_code_size);
        }
        exec->clinit_max_stack = cgen->builder->max_stack;
        exec->clinit_max_locals = cgen->builder->max_locals;
    }
}

static CS_Executable *code_generate(CS_Compiler *compiler, const char *class_name)
{
    CS_Executable *exec = (CS_Executable *)calloc(1, sizeof(CS_Executable));

    /* Find the FileDecl for this class */
    FileDecl *file_decl = NULL;
    if (compiler->header_store && class_name)
    {
        for (FileDecl *fd = compiler->header_store->files; fd; fd = fd->next)
        {
            if (fd->class_name && strcmp(fd->class_name, class_name) == 0)
            {
                file_decl = fd;
                compiler->current_file_decl = fd;
                break;
            }
        }
    }

    CodegenVisitor *cgen = create_codegen_visitor(compiler, exec, class_name);

    /* Generate code from FileDecl->functions (authoritative source) */
    if (file_decl)
    {
        for (FunctionDeclarationList *fl = file_decl->functions; fl; fl = fl->next)
        {
            FunctionDeclaration *f = fl->func;
            if (!f || !f->body)
            {
                continue;
            }

            code_output_reset_method(cgen->output);
            codegen_begin_function(cgen, f);
            codegen_traverse_stmt(f->body, cgen);
            codegen_finish_function(cgen);

            CS_Function *info = find_function_entry(cgen, f);
            finalize_function(cgen, info);
        }
    }

    /* Transfer constant pool ownership to exec */
    exec->cp = code_output_take_cp(cgen->output);

    exec->jvm_static_field_count = cgen->static_field_count;
    if (cgen->static_field_count)
    {
        exec->jvm_static_fields = (CG_StaticField *)calloc(
            cgen->static_field_count, sizeof(CG_StaticField));
        for (int i = 0; i < cgen->static_field_count; i++)
            exec->jvm_static_fields[i] = cgen->static_fields[i];
    }

    exec->jvm_class_def_count = cgen->class_def_count;
    if (cgen->class_def_count)
    {
        exec->jvm_class_defs = (CG_ClassDef *)calloc(
            cgen->class_def_count, sizeof(CG_ClassDef));
        for (int i = 0; i < cgen->class_def_count; i++)
            exec->jvm_class_defs[i] = cgen->class_defs[i];
    }

    exec->function_count = cgen->function_count;
    if (cgen->function_count)
    {
        exec->functions =
            (CS_Function *)calloc(cgen->function_count, sizeof(CS_Function));
        for (int i = 0; i < cgen->function_count; i++)
            exec->functions[i] = cgen->functions[i];
    }

    /* Transfer StackMapTable constant pool ownership */
    exec->stackmap_constant_pool = cgen->stackmap_cp;
    cgen->stackmap_cp = NULL;

    /* Generate synthetic main(String[] args) if user has main function */
    bool has_user_main = false;
    bool main_has_args = false;
    char *user_main_desc = NULL;
    for (int i = 0; i < exec->function_count; ++i)
    {
        if (exec->functions[i].is_jvm_main)
        {
            has_user_main = true;
            main_has_args = exec->functions[i].main_has_args;
            if (main_has_args)
            {
                user_main_desc = strdup("(I[L__charPtr;)I");
            }
            else
            {
                user_main_desc = strdup("()I");
            }
            break;
        }
    }

    if (has_user_main)
    {
        /* Add CP entries for synthetic main */
        int sm_user_main_idx = cp_builder_add_methodref(
            exec->cp, class_name, "main", user_main_desc);

        int sm_charptr_class_idx = 0;
        int sm_utf8_field_idx = 0;
        int sm_getbytes_idx = 0;
        int sm_ptr_init_idx = 0;
        int sm_ptr_base_field = 0;
        int sm_ptr_offset_field = 0;
        int sm_null_str_idx = 0;
        int sm_concat_idx = 0;

        if (main_has_args)
        {
            /* Mark char pointer usage for selective generation */
            ptr_usage_mark(PTR_TYPE_CHAR);

            sm_charptr_class_idx = cp_builder_add_class(exec->cp, "__charPtr");
            sm_utf8_field_idx = cp_builder_add_fieldref(
                exec->cp, "java/nio/charset/StandardCharsets", "UTF_8",
                "Ljava/nio/charset/Charset;");
            sm_getbytes_idx = cp_builder_add_methodref(
                exec->cp, "java/lang/String", "getBytes",
                "(Ljava/nio/charset/Charset;)[B");
            /* Add "\0" string and concat method for null-termination */
            char null_char = 0;
            sm_null_str_idx = cp_builder_add_string_len(exec->cp, &null_char, 1);
            sm_concat_idx = cp_builder_add_methodref(
                exec->cp, "java/lang/String", "concat",
                "(Ljava/lang/String;)Ljava/lang/String;");
            /* __charPtr fields for inline ptr creation */
            sm_ptr_init_idx = cp_builder_add_methodref(exec->cp, "__charPtr", "<init>", "()V");
            sm_ptr_base_field = cp_builder_add_fieldref(exec->cp, "__charPtr", "base", "[B");
            sm_ptr_offset_field = cp_builder_add_fieldref(exec->cp, "__charPtr", "offset", "I");
        }

        /* Build synthetic main bytecode using CodeBuilder */
        MethodCode *mc = method_code_create();
        CodeBuilder *cb = codebuilder_create(exec->cp, mc, true, NULL, NULL, "main");

        /* Set initial frame: local 0 = String[] args */
        codebuilder_set_local(cb, 0, cb_type_object("[Ljava/lang/String;"));

        if (main_has_args)
        {
            /* Java args doesn't include program name, but C argv[0] is program name */
            /* argc = args.length + 1 (include program name) */
            codebuilder_build_aload(cb, 0);
            codebuilder_build_arraylength(cb);
            codebuilder_build_iconst(cb, 1);
            codebuilder_build_iadd(cb);
            codebuilder_build_istore(cb, 1);

            /* argv = new __charPtr[argc] */
            codebuilder_build_iload(cb, 1);
            codebuilder_build_anewarray(cb, sm_charptr_class_idx);
            codebuilder_build_astore(cb, 2);

            /* argv[0] = "./program" (dummy program name) */
            codebuilder_build_aload(cb, 2);
            codebuilder_build_iconst(cb, 0);
            codebuilder_build_ldc(cb, sm_null_str_idx, CF_VAL_OBJECT);
            codebuilder_build_getstatic(cb, sm_utf8_field_idx);
            codebuilder_build_invokevirtual(cb, sm_getbytes_idx);
            codebuilder_build_iconst(cb, 0);
            codebuilder_emit_ptr_create_bytecode(cb, sm_charptr_class_idx, sm_ptr_init_idx,
                                                 sm_ptr_base_field, sm_ptr_offset_field);
            codebuilder_build_aastore(cb);

            /* i = 0 */
            codebuilder_build_iconst(cb, 0);
            codebuilder_build_istore(cb, 3);

            /* Loop: copy args[i] to argv[i+1] */
            CB_Label *loop_start = codebuilder_create_label(cb);
            CB_Label *loop_end = codebuilder_create_label(cb);
            codebuilder_mark_loop_header(cb, loop_start);
            codebuilder_place_label(cb, loop_start);

            /* if (i >= args.length) goto loop_end */
            codebuilder_build_iload(cb, 3);
            codebuilder_build_aload(cb, 0);
            codebuilder_build_arraylength(cb);
            codebuilder_jump_if_icmp(cb, ICMP_GE, loop_end);

            /* temp = __charPtr.create((args[i] + "\0").getBytes(UTF_8), 0) */
            codebuilder_build_aload(cb, 0);
            codebuilder_build_iload(cb, 3);
            codebuilder_build_aaload(cb);
            /* Null-terminate: args[i].concat("\0") */
            codebuilder_build_ldc(cb, sm_null_str_idx, CF_VAL_OBJECT);
            codebuilder_build_invokevirtual(cb, sm_concat_idx);
            codebuilder_build_getstatic(cb, sm_utf8_field_idx);
            codebuilder_build_invokevirtual(cb, sm_getbytes_idx);
            codebuilder_build_iconst(cb, 0);
            codebuilder_emit_ptr_create_bytecode(cb, sm_charptr_class_idx, sm_ptr_init_idx,
                                                 sm_ptr_base_field, sm_ptr_offset_field);
            codebuilder_build_astore(cb, 4);

            /* argv[i+1] = temp */
            codebuilder_build_aload(cb, 2);
            codebuilder_build_iload(cb, 3);
            codebuilder_build_iconst(cb, 1);
            codebuilder_build_iadd(cb);
            codebuilder_build_aload(cb, 4);
            codebuilder_build_aastore(cb);

            /* i++ */
            codebuilder_build_iinc(cb, 3, 1);

            /* goto loop_start */
            codebuilder_jump(cb, loop_start);

            /* loop_end: */
            codebuilder_place_label(cb, loop_end);

            /* Call user main(argc, argv) */
            codebuilder_build_iload(cb, 1);
            codebuilder_build_aload(cb, 2);
            codebuilder_build_invokestatic(cb, sm_user_main_idx);
            codebuilder_build_pop(cb);
            codebuilder_build_return(cb);
        }
        else
        {
            /* Simple wrapper: call main(), pop result, return */
            codebuilder_build_invokestatic(cb, sm_user_main_idx);
            codebuilder_build_pop(cb);
            codebuilder_build_return(cb);
        }

        codebuilder_resolve_jumps(cb);

        /* Add synthetic main to exec->functions */
        int new_count = exec->function_count + 1;
        CS_Function *new_funcs = (CS_Function *)calloc(new_count, sizeof(CS_Function));
        for (int i = 0; i < exec->function_count; i++)
        {
            new_funcs[i] = exec->functions[i];
        }
        exec->functions = new_funcs;
        CS_Function *sm = &exec->functions[exec->function_count];
        sm->name = strdup("main");
        sm->decl = NULL;
        sm->signature_kind = CS_FUNC_SIG_JVM_MAIN_WRAPPER;
        sm->code_size = method_code_size(mc);
        if (sm->code_size > 0)
        {
            sm->code = (uint8_t *)calloc(sm->code_size, sizeof(uint8_t));
            memcpy(sm->code, method_code_data(mc), sm->code_size);
        }
        sm->max_stack = cb->max_stack;
        sm->max_locals = cb->max_locals;
        /* is_jvm_main = 0: Not user's main, but the entry point */
        sm->constant_pool_index = -1;

        /* Generate StackMapTable for synthetic main */
        if (main_has_args)
        {
            int frame_count = 0;
            sm->stack_map_frames = codebuilder_generate_stackmap(
                cb, exec->stackmap_constant_pool, &frame_count);
            sm->stack_map_frame_count = frame_count;
        }

        exec->function_count++;

        codebuilder_destroy(cb);
        method_code_destroy(mc);
        free(user_main_desc);
    }

    /* Generate <clinit> bytecode using visitor (before visitor is destroyed) */
    generate_clinit_code(cgen, exec);

    delete_visitor((Visitor *)cgen);
    return exec;
}

static void free_executable(CS_Executable *exec)
{
    if (!exec)
    {
        return;
    }

    if (exec->cp)
    {
        /* Free individual constant pool entry contents */
        CP_Constant *entries = cp_builder_entries(exec->cp);
        int count = cp_builder_count(exec->cp);
        for (int i = 0; i < count; ++i)
        {
            CP_Constant *c = &entries[i];
            switch (c->type)
            {
            case CP_CONST_STRING:
                free(c->u.c_string.data);
                break;
            case CP_CONST_METHOD:
                free(c->u.c_method.name);
                break;
            case CP_CONST_FIELD:
                if (c->u.c_field.class_name)
                {
                    free(c->u.c_field.class_name);
                }
                free(c->u.c_field.name);
                break;
            case CP_CONST_CLASS:
                free(c->u.c_class.name);
                break;
            default:
                break;
            }
        }
        cp_builder_destroy(exec->cp);
    }

    if (exec->jvm_static_fields)
    {
        free(exec->jvm_static_fields);
    }
    if (exec->jvm_class_defs)
    {
        for (int i = 0; i < exec->jvm_class_def_count; ++i)
        {
            CG_ClassDef *cd = &exec->jvm_class_defs[i];
            free(cd->name);
            if (cd->fields)
            {
                for (int j = 0; j < cd->field_count; ++j)
                {
                    free(cd->fields[j].name);
                }
                free(cd->fields);
            }
        }
        free(exec->jvm_class_defs);
    }
    if (exec->functions)
    {
        for (int i = 0; i < exec->function_count; ++i)
        {
            if (exec->functions[i].code)
            {
                free(exec->functions[i].code);
            }
            /* Free StackMapTable frames */
            if (exec->functions[i].stack_map_frames)
            {
                codebuilder_free_stackmap(exec->functions[i].stack_map_frames,
                                          exec->functions[i].stack_map_frame_count);
            }
        }
        free(exec->functions);
    }

    if (exec->stackmap_constant_pool)
    {
        cf_cp_free(exec->stackmap_constant_pool);
    }

    if (exec->clinit_code)
    {
        free(exec->clinit_code);
    }

    free(exec);
}

static int compute_code_size(CS_Executable *exec)
{
    int code_size = 0;
    for (int i = 0; i < exec->function_count; ++i)
    {
        code_size += exec->functions[i].code_size;
    }

    return code_size;
}

static int *build_stackmap_cp_map(const CF_ConstantPool *source, CF_ConstantPool *dest)
{
    if (!source || !dest || source->count == 0)
    {
        return NULL;
    }

    int *map = (int *)calloc(source->count, sizeof(int));

    for (int i = 1; i < source->count; ++i)
    {
        const CF_ConstantEntry *entry = &source->entries[i];
        if (entry->tag != CP_TAG_CLASS)
        {
            continue;
        }

        int name_index = entry->u.index;
        if (name_index == 0 || name_index >= source->count)
        {
            continue;
        }

        const CF_ConstantEntry *utf8 = &source->entries[name_index];
        if (utf8->tag != CP_TAG_UTF8 || utf8->u.utf8.length == 0 || !utf8->u.utf8.bytes)
        {
            continue;
        }

        char *name = (char *)calloc(utf8->u.utf8.length + 1, sizeof(char));
        memcpy(name, utf8->u.utf8.bytes, utf8->u.utf8.length);
        name[utf8->u.utf8.length] = '\0';

        map[i] = cf_cp_add_class(dest, name);
    }

    return map;
}

static void remap_stackmap_frames(CF_StackMapFrame *frames, int frame_count,
                                  int *cp_map, int map_count)
{
    if (!frames || !cp_map || map_count == 0)
    {
        return;
    }

    for (int i = 0; i < frame_count; ++i)
    {
        CF_StackMapFrame *frame = &frames[i];

        for (int j = 0; j < frame->locals_count; ++j)
        {
            CF_VerificationTypeInfo *vt = &frame->locals[j];
            if (vt->tag == CF_VERIFICATION_OBJECT && vt->u.cpool_index < map_count &&
                cp_map[vt->u.cpool_index])
            {
                vt->u.cpool_index = (uint16_t)cp_map[vt->u.cpool_index];
            }
        }

        for (int j = 0; j < frame->stack_count; ++j)
        {
            CF_VerificationTypeInfo *vt = &frame->stack[j];
            if (vt->tag == CF_VERIFICATION_OBJECT && vt->u.cpool_index < map_count &&
                cp_map[vt->u.cpool_index])
            {
                vt->u.cpool_index = (uint16_t)cp_map[vt->u.cpool_index];
            }
        }
    }
}

static const char *field_type_descriptor(CG_ClassField *field)
{
    if (field && field->type_spec)
    {
        return cg_jvm_descriptor(field->type_spec);
    }
    return CF_DESC_INT;
}

static bool field_is_object_reference(CG_ClassField *field)
{
    if (!field || !field->type_spec)
    {
        return false;
    }
    CG_JVMRefKind ref_kind = cg_jvm_ref_kind(field->type_spec);
    return ref_kind == CG_JVM_REF_OBJECT || ref_kind == CG_JVM_REF_POINTER;
}

/* Check if field is a fixed-size array that needs initialization */
static bool field_is_fixed_array(CG_ClassField *field)
{
    if (!field || !field->type_spec)
    {
        return false;
    }
    if (!cs_type_is_array(field->type_spec))
    {
        return false;
    }
    int len = array_length_from_type(field->type_spec);
    return len > 0;
}

static char *field_class_name(CG_ClassField *field)
{
    if (!field || !field->type_spec)
    {
        return NULL;
    }
    if (cs_type_is_pointer(field->type_spec))
    {
        PtrTypeIndex ptr_index = (PtrTypeIndex)cg_pointer_runtime_kind(field->type_spec);
        return strdup(ptr_type_class_name(ptr_index));
    }
    const char *name = cs_type_user_type_name(field->type_spec);
    if (!name || !name[0])
    {
        return NULL;
    }
    return strdup(name);
}
/* Build method descriptor from function info */
static char *build_function_descriptor(CS_Function *fn, CS_Executable *exec)
{
    if (!fn)
    {
        return strdup("()V");
    }

    if (fn->signature_kind == CS_FUNC_SIG_JVM_MAIN_WRAPPER)
    {
        return strdup("([Ljava/lang/String;)V");
    }

    if (fn->signature_kind == CS_FUNC_SIG_C_MAIN)
    {
        if (fn->main_has_args)
        {
            return strdup("(I[L__charPtr;)I");
        }
        return strdup("()I");
    }

    if (fn->signature_kind == CS_FUNC_SIG_FROM_DECL && fn->decl)
    {
        return (char *)cg_jvm_method_descriptor(fn->decl);
    }

    (void)exec;
    return strdup("()V");
}

static void serialize_classfile(CS_Executable *exec, const char *class_name)
{
    char output_path[OUTPUT_PATH_MAX];
    const char *safe_class_name = class_name ? class_name : "Main";

    snprintf(output_path, sizeof output_path, "%s.class", safe_class_name);

    /* Take ownership of the constant pool from exec->cp.
     * Indices are already final since cp_builder directly uses CF_ConstantPool. */
    CF_ConstantPool *cp = cp_builder_take_cf_cp(exec->cp);

    /* Initialize class file builder with existing constant pool */
    CF_Builder *builder = cf_builder_create_from_cp(safe_class_name, cp);

    /* Set SourceFile attribute for debugging */
    char source_file[256];
    snprintf(source_file, sizeof source_file, "%s.c", safe_class_name);
    cf_builder_set_source_file(builder, source_file);

    int sm_cp_map_count = exec->stackmap_constant_pool->count;
    int *sm_cp_map = build_stackmap_cp_map(exec->stackmap_constant_pool,
                                           builder->cf->constant_pool);

    /* Add static fields */
    for (int i = 0; i < exec->jvm_static_field_count; ++i)
    {
        CG_StaticField *field = &exec->jvm_static_fields[i];
        Declaration *decl = field->decl;
        const char *field_name = decl ? decl->name : "field_unknown";
        /* Use cg_jvm_descriptor for proper array/struct type descriptors */
        const char *desc = decl && decl->type ? cg_jvm_descriptor(decl->type)
                                              : (field->type_spec ? cg_jvm_descriptor(field->type_spec)
                                                                  : CF_DESC_INT);
        /* static keyword in C -> private in JVM, non-static -> public (external linkage) */
        int access = ACC_STATIC;
        access |= (decl && decl->is_static) ? ACC_PRIVATE : ACC_PUBLIC;
        cf_builder_add_field(builder, access, field_name, desc);
    }

    /* Add methods */
    for (int i = 0; i < exec->function_count; ++i)
    {
        CS_Function *fn = &exec->functions[i];

        int access = ACC_PUBLIC | ACC_STATIC;
        if (fn->is_native)
        {
            access |= ACC_NATIVE;
        }

        /* User's main is private (called by synthetic main wrapper) */
        /* static functions are also private */
        if (fn->is_jvm_main || fn->is_static)
        {
            access = ACC_PRIVATE | ACC_STATIC;
        }

        char *desc = build_function_descriptor(fn, exec);
        int method_idx = cf_builder_begin_method(builder, access, fn->name, desc);

        if (fn->code && fn->code_size > 0 && !fn->is_native)
        {
            /* CP indices are already final - no transformation needed */
            cf_builder_set_code(builder, method_idx,
                                fn->max_stack,
                                fn->max_locals,
                                fn->code,
                                fn->code_size);

            /* Add StackMapTable frames if available */
            if (fn->stack_map_frames && fn->stack_map_frame_count > 0)
            {
                remap_stackmap_frames(fn->stack_map_frames,
                                      fn->stack_map_frame_count,
                                      sm_cp_map, sm_cp_map_count);
                cf_builder_set_stack_map_table(builder, method_idx,
                                               fn->stack_map_frames,
                                               fn->stack_map_frame_count);
            }

            /* Add LineNumberTable if available */
            if (fn->line_numbers && fn->line_number_count > 0)
            {
                cf_builder_set_line_number_table(builder, method_idx,
                                                 fn->line_numbers,
                                                 fn->line_number_count);
            }
        }

        free(desc);
    }

    /* Add clinit helper methods if <clinit> was split */
    for (int p = 0; p < exec->clinit_part_count; p++)
    {
        CS_ClinitPart *part = &exec->clinit_parts[p];
        char *part_name = make_clinit_part_name(p);

        int part_idx = cf_builder_begin_method(builder, ACC_PRIVATE | ACC_STATIC,
                                               part_name, "()V");
        cf_builder_set_code(builder, part_idx, part->max_stack,
                            part->max_locals, part->code, part->code_size);
    }

    /* Add <clinit> method if static field initialization code was generated */
    if (exec->clinit_code && exec->clinit_code_size > 0)
    {
        /* CP indices are already final - no transformation needed */
        int clinit_idx = cf_builder_begin_method(builder, ACC_STATIC, "<clinit>", "()V");
        cf_builder_set_code(builder, clinit_idx, exec->clinit_max_stack,
                            exec->clinit_max_locals,
                            exec->clinit_code,
                            exec->clinit_code_size);
    }

    /* Write the class file */
    if (!cf_write_to_file(builder->cf, output_path))
    {
        fprintf(stderr, "failed to write class file: %s\n", output_path);
        exit(1);
    }

    /* Clean up */
    if (sm_cp_map)
    {
        free(sm_cp_map);
    }
    cf_builder_destroy(builder);
}

/* Track generated struct class files to avoid duplicates */
typedef struct GeneratedClass_tag
{
    char *name;
    struct GeneratedClass_tag *next;
} GeneratedClass;

static GeneratedClass *generated_classes = NULL;

static bool is_class_generated(const char *name)
{
    for (GeneratedClass *gc = generated_classes; gc; gc = gc->next)
    {
        if (strcmp(gc->name, name) == 0)
            return true;
    }
    return false;
}

static void mark_class_generated(const char *name)
{
    if (is_class_generated(name))
        return;
    GeneratedClass *gc = (GeneratedClass *)calloc(1, sizeof(GeneratedClass));
    gc->name = strdup(name);
    gc->next = generated_classes;
    generated_classes = gc;
}

static void free_generated_classes()
{
    while (generated_classes)
    {
        GeneratedClass *next = generated_classes->next;
        free(generated_classes->name);
        free(generated_classes);
        generated_classes = next;
    }
}

/* Generate a class file for a synthetic struct definition */
static void serialize_struct_classfile(CG_ClassDef *class_def)
{
    /* Skip if already generated */
    if (is_class_generated(class_def->name))
        return;
    mark_class_generated(class_def->name);

    char output_path[OUTPUT_PATH_MAX];
    snprintf(output_path, sizeof output_path, "%s.class", class_def->name);

    /* Initialize class file builder */
    CF_Builder *builder = cf_builder_create(class_def->name);

    /* Add instance fields for each struct member */
    for (int i = 0; i < class_def->field_count; ++i)
    {
        CG_ClassField *field = &class_def->fields[i];
        const char *desc = field_type_descriptor(field);
        cf_builder_add_field(builder, ACC_PUBLIC, field->name, desc);
    }

    /* Count reference type fields that need initialization */
    int ref_field_count = 0;
    for (int i = 0; i < class_def->field_count; ++i)
    {
        CG_ClassField *field = &class_def->fields[i];
        if (field_is_object_reference(field))
            ref_field_count++;
    }

    /* Count fixed-size array fields that need initialization */
    int array_field_count = 0;
    for (int i = 0; i < class_def->field_count; ++i)
    {
        CG_ClassField *field = &class_def->fields[i];
        if (field_is_fixed_array(field))
            array_field_count++;
    }

    /* Add default constructor: <init>()V
     * This calls super.<init>() and initializes reference type fields and array fields.
     * Bytecode:
     *   aload_0                      ; push 'this'
     *   invokespecial Object.<init>  ; call super()
     *   ; For each reference type field:
     *   aload_0                      ; push 'this'
     *   new FieldClass               ; new FieldClass()
     *   dup
     *   invokespecial FieldClass.<init>
     *   putfield this.fieldName
     *   ; For each fixed-size array field:
     *   aload_0                      ; push 'this'
     *   sipush N                     ; push array length
     *   newarray/anewarray           ; create array
     *   putfield this.fieldName
     *   return
     */
    int object_init_idx = cf_cp_add_methodref(builder->cf->constant_pool,
                                              "java/lang/Object", "<init>", "()V");

    /* Calculate code size:
     * - 5 (base: aload_0, invokespecial, u2, return)
     * - 12 per ref field (aload_0 + new + dup + invokespecial + putfield)
     * - 10 per array field (aload_0 + sipush/iconst + newarray/anewarray + putfield)
     */
    int code_size = 5 + ref_field_count * 12 + array_field_count * 10;
    uint8_t *init_code = (uint8_t *)calloc(code_size, sizeof(uint8_t));
    int pc = 0;

    init_code[pc++] = CF_ALOAD_0;       /* aload_0 */
    init_code[pc++] = CF_INVOKESPECIAL; /* invokespecial Object.<init> */
    init_code[pc++] = (uint8_t)(object_init_idx >> 8);
    init_code[pc++] = (uint8_t)object_init_idx;

    /* Initialize reference type fields */
    for (int i = 0; i < class_def->field_count; ++i)
    {
        CG_ClassField *field = &class_def->fields[i];
        if (!field_is_object_reference(field))
            continue;

        char *field_class = field_class_name(field);
        if (!field_class)
            continue;

        int class_idx = cf_cp_add_class(builder->cf->constant_pool, field_class);
        int field_init_idx = cf_cp_add_methodref(builder->cf->constant_pool,
                                                 field_class, "<init>", "()V");
        int field_ref_idx = cf_cp_add_fieldref(builder->cf->constant_pool,
                                               class_def->name, field->name,
                                               field_type_descriptor(field));

        init_code[pc++] = CF_ALOAD_0; /* aload_0 */
        init_code[pc++] = CF_NEW;     /* new */
        init_code[pc++] = (uint8_t)(class_idx >> 8);
        init_code[pc++] = (uint8_t)class_idx;
        init_code[pc++] = CF_DUP;           /* dup */
        init_code[pc++] = CF_INVOKESPECIAL; /* invokespecial */
        init_code[pc++] = (uint8_t)(field_init_idx >> 8);
        init_code[pc++] = (uint8_t)field_init_idx;
        init_code[pc++] = CF_PUTFIELD; /* putfield */
        init_code[pc++] = (uint8_t)(field_ref_idx >> 8);
        init_code[pc++] = (uint8_t)field_ref_idx;

        free(field_class);
    }

    /* Initialize fixed-size array fields */
    for (int i = 0; i < class_def->field_count; ++i)
    {
        CG_ClassField *field = &class_def->fields[i];
        if (!field_is_fixed_array(field))
            continue;

        int arr_len = array_length_from_type(field->type_spec);
        TypeSpecifier *elem_type = cs_type_child(field->type_spec);

        int field_ref_idx = cf_cp_add_fieldref(builder->cf->constant_pool,
                                               class_def->name, field->name,
                                               field_type_descriptor(field));

        init_code[pc++] = CF_ALOAD_0; /* aload_0 */

        /* Push array length */
        if (arr_len >= -128 && arr_len <= 127)
        {
            init_code[pc++] = CF_BIPUSH;
            init_code[pc++] = (uint8_t)arr_len;
        }
        else
        {
            init_code[pc++] = CF_SIPUSH;
            init_code[pc++] = (uint8_t)(arr_len >> 8);
            init_code[pc++] = (uint8_t)arr_len;
        }

        /* Create array */
        if (cs_type_is_primitive(elem_type) || cs_type_is_enum(elem_type))
        {
            /* Primitive array: newarray */
            int atype = newarray_type_code(elem_type);
            init_code[pc++] = CF_NEWARRAY;
            init_code[pc++] = (uint8_t)atype;
        }
        else
        {
            /* Object/struct array: anewarray */
            const char *elem_class = cs_type_user_type_name(elem_type);
            if (!elem_class)
                elem_class = "java/lang/Object";
            int class_idx = cf_cp_add_class(builder->cf->constant_pool, elem_class);
            init_code[pc++] = CF_ANEWARRAY;
            init_code[pc++] = (uint8_t)(class_idx >> 8);
            init_code[pc++] = (uint8_t)class_idx;
        }

        init_code[pc++] = CF_PUTFIELD;
        init_code[pc++] = (uint8_t)(field_ref_idx >> 8);
        init_code[pc++] = (uint8_t)field_ref_idx;
    }

    init_code[pc++] = CF_RETURN; /* return */

    int init_method_idx = cf_builder_begin_method(builder,
                                                  ACC_PUBLIC,
                                                  "<init>",
                                                  "()V");
    cf_builder_set_code(builder, init_method_idx,
                        3, /* max_stack: 3 for array creation */
                        1, /* max_locals: 1 for 'this' */
                        init_code,
                        pc);

    free(init_code);

    /* Write the class file */
    if (!cf_write_to_file(builder->cf, output_path))
    {
        fprintf(stderr, "failed to write struct class file: %s\n", output_path);
        exit(1);
    }

    cf_builder_destroy(builder);
}

/* Generate class files for all synthetic structs */
static void serialize_struct_classfiles(CS_Executable *exec)
{
    for (int i = 0; i < exec->jvm_class_def_count; ++i)
    {
        serialize_struct_classfile(&exec->jvm_class_defs[i]);
    }
}

/*
 * Dependency resolution is now handled automatically by the preprocessor.
 * When a .h file is included, the preprocessor adds the corresponding .c
 * file to the dependency list. Synthetic headers (like stdio.h) automatically
 * trigger compilation of their embedded implementation.
 */

int main(int argc, char *argv[])
{
    if (argc <= 1)
    {
        printf("Usage: ./codegen <source> [source2 ...]\n");
        return 1;
    }

    CompilerContext *ctx = compiler_context_create();

    /* Compile all source files independently */
    for (int i = 1; i < argc; i++)
    {
        if (!CS_compile(ctx, argv[i], false))
        {
            fprintf(stderr, "compile failed: %s\n", argv[i]);
            compiler_context_destroy(ctx);
            return 1;
        }
    }

    /* Create TU for codegen (functions are in FileDecl->functions) */
    TranslationUnit *compiler = tu_create(ctx, NULL);
    compiler->stmt_list = ctx->all_statements;
    compiler->decl_list = ctx->all_declarations;

    /* Add all FileDecls to header_index (codegen needs full visibility) */
    for (FileDecl *fd = ctx->header_store->files; fd; fd = fd->next)
    {
        header_index_add_file(compiler->header_index, fd);
    }

    /* Initialize pointer usage tracking for selective generation */
    g_ptr_usage = (PtrUsage *)calloc(1, sizeof(PtrUsage));
    ptr_usage_init(g_ptr_usage);

    /* Process compiled sources and generate code.
     * Loop until no new sources are added (lazy-loaded helpers may add sources). */
    bool made_progress = true;
    while (made_progress)
    {
        made_progress = false;

        /* Compile any pending sources (added by lazy-loaded helpers) */
        while (ctx->pending_sources)
        {
            CS_PendingDependency *dep = ctx->pending_sources;
            ctx->pending_sources = dep->next;
            dep->next = NULL;

            /* Use internal compile - includes per-TU mean_check */
            if (!compile_source_for_codegen(ctx, dep->path, dep->is_embedded))
            {
                fprintf(stderr, "compile failed: %s\n", dep->path);
                free(dep->path);
                free(dep);
                free_generated_classes();
                compiler_context_destroy(ctx);
                return 1;
            }
            free(dep->path);
            free(dep);
            made_progress = true;
        }

        /* Per-TU mean_check is now done inside compile_source_for_codegen.
         * Update TU with new aggregated data. */
        if (made_progress)
        {
            compiler->stmt_list = ctx->all_statements;
            compiler->decl_list = ctx->all_declarations;
        }

        /* Generate code for all compiled sources */
        for (CS_PendingDependency *dep = ctx->compiled_deps; dep; dep = dep->next)
        {
            /* Only generate code for .c files, not headers */
            int len = strlen(dep->path);
            if (len < 2 || strcmp(dep->path + len - 2, ".c") != 0)
                continue;

            /* Get class_name from FileDecl (already converted from path) */
            FileDecl *fd = header_store_find(ctx->header_store, dep->path);
            if (!fd || !fd->class_name)
                continue;
            const char *class_name = fd->class_name;

            /* Skip if class already generated */
            if (is_class_generated(class_name))
                continue;
            mark_class_generated(class_name);
            made_progress = true;

            CS_Executable *exec = code_generate(compiler, class_name);

            serialize_classfile(exec, class_name);
            serialize_struct_classfiles(exec);

            free_executable(exec);
        }
    }

    /* Generate synthetic pointer struct classes */
    generate_ptr_struct_classes_selective(g_ptr_usage);

    free_generated_classes();
    compiler_context_destroy(ctx);
    return 0;
}
