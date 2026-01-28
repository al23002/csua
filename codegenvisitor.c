#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "classfile.h"
#include "ast.h"
#include "compiler.h"
#include "executable.h"
#include "util.h"
#include "header_store.h"
#include "codebuilder_core.h"
#include "codebuilder_frame.h"
#include "codebuilder_label.h"
#include "codebuilder_part1.h"
#include "codebuilder_part2.h"
#include "codebuilder_part3.h"
#include "codebuilder_stackmap.h"
#include "codebuilder_types.h"
#include "codegen_constants.h"
#include "codegen_symbols.h"
#include "codegen_jvm_types.h"
#include "codegenvisitor.h"
#include "codebuilder_ptr.h"
#include "codebuilder_control.h"
#include "codegenvisitor_util.h"
#include "codegenvisitor_stmt_util.h"
#include "cminor_type.h"
#include "parsed_type.h"
#include "synthetic_codegen.h"

static void ensure_static_capacity(CodegenVisitor *v, int need)
{
    if (v->static_field_count + need <= v->static_field_capacity)
    {
        return;
    }
    int new_cap = v->static_field_capacity ? v->static_field_capacity * 2 : 4;
    while (new_cap < v->static_field_count + need)
    {
        new_cap *= 2;
    }
    CG_StaticField *new_fields = (CG_StaticField *)calloc(new_cap, sizeof(CG_StaticField));
    for (int i = 0; i < v->static_field_count; i++)
    {
        new_fields[i] = v->static_fields[i];
    }
    v->static_fields = new_fields;
    v->static_field_capacity = new_cap;
}

static void ensure_function_capacity(CodegenVisitor *v, int need)
{
    if (v->function_count + need <= v->function_capacity)
    {
        return;
    }

    int new_cap = v->function_capacity ? v->function_capacity * 2 : 4;
    while (new_cap < v->function_count + need)
    {
        new_cap *= 2;
    }

    CS_Function *new_funcs = (CS_Function *)calloc(new_cap, sizeof(CS_Function));
    for (int i = 0; i < v->function_count; i++)
    {
        new_funcs[i] = v->functions[i];
    }
    v->functions = new_funcs;
    v->function_capacity = new_cap;
}

static void register_static_fields(CodegenVisitor *v)
{
    DeclarationList *decls = v->compiler->decl_list;
    while (decls)
    {
        Declaration *decl = decls->decl;
        if (!decl || !decl->class_name || !v->current_class_name ||
            strcmp(decl->class_name, v->current_class_name) != 0)
        {
            decls = decls->next;
            continue;
        }
        /* Skip extern declarations - they reference fields in other classes */
        if (decl->is_extern)
        {
            decls = decls->next;
            continue;
        }
        ensure_static_capacity(v, 1);
        CG_StaticField *field = &v->static_fields[v->static_field_count++];
        field->decl = decl;
        field->type_spec = decl->type;
        decl->index = v->static_field_count - 1;
        decls = decls->next;
    }
}

static void ensure_class_def_capacity(CodegenVisitor *v, int need)
{
    if (v->class_def_count + need <= v->class_def_capacity)
    {
        return;
    }
    int new_cap = v->class_def_capacity ? v->class_def_capacity * 2 : 4;
    while (new_cap < v->class_def_count + need)
    {
        new_cap *= 2;
    }
    CG_ClassDef *new_defs = (CG_ClassDef *)calloc(new_cap, sizeof(CG_ClassDef));
    for (int i = 0; i < v->class_def_count; i++)
    {
        new_defs[i] = v->class_defs[i];
    }
    v->class_defs = new_defs;
    v->class_def_capacity = new_cap;
}

static int count_struct_members(StructMember *members)
{
    int count = 0;
    for (; members; members = members->next)
    {
        ++count;
    }
    return count;
}

/* Register a single struct definition as a class definition.
 * Returns true if registered, false if already exists or invalid. */
static bool register_single_struct_def(CodegenVisitor *v, StructDefinition *def)
{
    if (!def || !def->id.name)
        return false;

    /* All structs now have names (including anonymous ones like "Foo$0") */
    const char *name = def->id.name;

    /* Check if already registered (avoid duplicates from headers) */
    if (find_class_index(v, name) != -1)
    {
        return false;
    }

    ensure_class_def_capacity(v, 1);
    CG_ClassDef *cd = &v->class_defs[v->class_def_count++];
    cd->name = strdup(name);

    /* Check if this is a union with special handling */
    if (def->is_union)
    {
        CS_UnionKind kind = cs_union_kind_from_members(def->members);
        switch (kind)
        {
        case CS_UNION_KIND_TYPE_PUNNING_INT_FLOAT:
            /* Single int field named "_bits" */
            cd->field_count = 1;
            cd->fields = (CG_ClassField *)calloc(1, sizeof(CG_ClassField));
            cd->fields[0].name = strdup("_bits");
            cd->fields[0].type_spec = cs_create_type_specifier(CS_INT_TYPE);
            break;

        case CS_UNION_KIND_TYPE_PUNNING_LONG_DOUBLE:
            /* Single long field named "_bits" */
            cd->field_count = 1;
            cd->fields = (CG_ClassField *)calloc(1, sizeof(CG_ClassField));
            cd->fields[0].name = strdup("_bits");
            cd->fields[0].type_spec = cs_create_type_specifier(CS_LONG_TYPE);
            break;

        case CS_UNION_KIND_REFERENCE:
            /* Single Object field named "_ref" for all reference types */
            /* Handles: pointers, aggregates (struct/union), and boxed primitives */
            cd->field_count = 1;
            cd->fields = (CG_ClassField *)calloc(1, sizeof(CG_ClassField));
            cd->fields[0].name = strdup("_ref");
            cd->fields[0].type_spec = cs_create_named_type_specifier(
                CS_STRUCT_TYPE, strdup("java/lang/Object"));
            break;

        default:
            /* UNSUPPORTED: should not happen with current implementation */
            fprintf(stderr, "Warning: unsupported union kind %d for %s\n", kind, name);
            goto register_all_fields;
        }
    }
    else
    {
    register_all_fields:
        cd->field_count = count_struct_members(def->members);
        if (cd->field_count > 0)
        {
            cd->fields = (CG_ClassField *)calloc(cd->field_count,
                                                 sizeof(CG_ClassField));
            int i = 0;
            for (StructMember *m = def->members; m; m = m->next, ++i)
            {
                /* m->type should already be resolved during struct registration */
                cd->fields[i].name = strdup(m->name);
                cd->fields[i].type_spec = m->type;
            }
        }
        else
        {
            cd->fields = NULL;
        }
    }
    return true;
}

/* Register struct definitions from a FileDecl */
static void register_structs_from_file(CodegenVisitor *v, FileDecl *fd)
{
    if (!fd)
        return;
    for (int idx = 0; idx < fd->struct_count; idx++)
    {
        register_single_struct_def(v, fd->structs[idx]);
    }
}

static void register_struct_definitions(CodegenVisitor *v)
{
    /* Register structs from current file */
    register_structs_from_file(v, v->compiler->current_file_decl);

    /* Register structs from all headers in header_store */
    if (v->compiler->header_store)
    {
        for (FileDecl *fd = v->compiler->header_store->files; fd; fd = fd->next)
        {
            if (fd != v->compiler->current_file_decl)
            {
                register_structs_from_file(v, fd);
            }
        }
    }
}

/* Check if function is a Java intrinsic (get_static/invoke_virtual/invoke_static/invoke_special/new) -
 * these should not generate actual method definitions in the class file */
static bool is_java_intrinsic_function(FunctionDeclaration *func)
{
    if (!func)
    {
        return false;
    }
    return find_attribute(func->attributes, CS_ATTRIBUTE_GET_STATIC) != NULL ||
           find_attribute(func->attributes, CS_ATTRIBUTE_INVOKE_VIRTUAL) != NULL ||
           find_attribute(func->attributes, CS_ATTRIBUTE_INVOKE_STATIC) != NULL ||
           find_attribute(func->attributes, CS_ATTRIBUTE_INVOKE_SPECIAL) != NULL ||
           find_attribute(func->attributes, CS_ATTRIBUTE_GET_FIELD) != NULL ||
           find_attribute(func->attributes, CS_ATTRIBUTE_NEW) != NULL ||
           find_attribute(func->attributes, CS_ATTRIBUTE_ARRAYLENGTH) != NULL ||
           find_attribute(func->attributes, CS_ATTRIBUTE_AALOAD) != NULL;
}

static void register_functions(CodegenVisitor *v)
{
    if (!v->compiler->current_file_decl)
        return;

    FunctionDeclarationList *funcs = v->compiler->current_file_decl->functions;
    while (funcs)
    {
        FunctionDeclaration *func = funcs->func;
        if (func)
        {
            const char *class_name = func->class_name ? func->class_name : v->current_class_name;
            if (!class_name || !v->current_class_name ||
                strcmp(class_name, v->current_class_name) != 0)
            {
                funcs = funcs->next;
                continue;
            }

            /* Skip Java intrinsic functions - they are not real methods */
            if (is_java_intrinsic_function(func))
            {
                funcs = funcs->next;
                continue;
            }

            /* Skip functions without body (prototype declarations only) */
            if (!func->body)
            {
                funcs = funcs->next;
                continue;
            }

            int argc = cs_count_parameters(func->param);
            bool is_jvm_main = cg_is_jvm_main_function(func);

            /* JVM main takes 1 argument (String[] args) regardless of C signature */
            if (is_jvm_main)
            {
                argc = 1;
            }

            int idx = cg_add_method(v, func);
            func->index = (int)idx;

            CS_Function *info = NULL;
            for (int i = 0; i < v->function_count; ++i)
            {
                if (v->functions[i].constant_pool_index == (int32_t)idx)
                {
                    info = &v->functions[i];
                    break;
                }
            }
            if (!info)
            {
                ensure_function_capacity(v, 1);
                info = &v->functions[v->function_count++];
                info->constant_pool_index = (int32_t)idx;
            }
            info->name = strdup(resolve_function_name(func));
            info->decl = func;
            info->arg_count = argc;
            info->is_native = false;
            info->is_jvm_main = is_jvm_main;
            info->is_static = func->is_static;

            /* Check if main takes argc/argv */
            bool main_has_args = is_jvm_main && cg_main_has_argc_argv(func);
            info->main_has_args = main_has_args;

            if (is_jvm_main)
            {
                /* cminor_main uses C-style signature, not JVM String[] */
                info->signature_kind = CS_FUNC_SIG_C_MAIN;
            }
            else
            {
                info->signature_kind = CS_FUNC_SIG_FROM_DECL;
            }
        }
        funcs = funcs->next;
    }
}

static void ensure_bytecode_capacity(CodegenVisitor *v)
{
    if (v->bytecode_count + 1 <= v->bytecode_capacity)
    {
        return;
    }

    int new_cap = v->bytecode_capacity ? v->bytecode_capacity * 2 : 32;
    BytecodeInstr *new_bc = (BytecodeInstr *)calloc(new_cap, sizeof(BytecodeInstr));
    for (int i = 0; i < v->bytecode_count; i++)
    {
        new_bc[i] = v->bytecode[i];
    }
    v->bytecode = new_bc;
    v->bytecode_capacity = new_cap;
}

void cg_record_bytecode(CodegenVisitor *v, CF_Opcode opcode, int pc,
                        int length)
{
    if (!v)
    {
        return;
    }

    ensure_bytecode_capacity(v);
    BytecodeInstr *instr = &v->bytecode[v->bytecode_count];
    instr->pc = pc;
    instr->length = length;
    instr->opcode = opcode;

    /* Frame state tracking is now handled by CodeBuilder */

    v->last_bytecode_index = v->bytecode_count;
    v->has_last_bytecode = true;
    ++v->bytecode_count;
}

static char *build_method_descriptor_from_strings(const char *return_desc,
                                                  const char **param_descs,
                                                  uint8_t param_count)
{
    const char *ret = return_desc ? return_desc : "V";
    int ret_len = strlen(ret);
    int param_len = 0;
    for (uint8_t i = 0; i < param_count; ++i)
    {
        const char *param = param_descs[i] ? param_descs[i] : "I";
        param_len += strlen(param);
    }

    int total = 1 + param_len + 1 + ret_len + 1;
    char *desc = (char *)calloc(total, sizeof(char));
    desc[0] = '(';

    int offset = 1;
    for (uint8_t i = 0; i < param_count; ++i)
    {
        const char *param = param_descs[i] ? param_descs[i] : "I";
        int len = strlen(param);
        memcpy(desc + offset, param, len);
        offset += len;
    }

    desc[offset++] = ')';
    memcpy(desc + offset, ret, ret_len);
    desc[offset + ret_len] = '\0';
    return desc;
}

void codegen_begin_function(CodegenVisitor *v, FunctionDeclaration *func)
{
    v->current_function = func;

    /* Calculate varargs_index for variadic functions */
    if (func && func->is_variadic)
    {
        int slot_index = 0;
        for (ParameterList *p = func->param; p && !p->is_ellipsis; p = p->next)
        {
            if (p->type && (cs_type_is_long_exact(p->type) || cs_type_is_double_exact(p->type)))
            {
                slot_index += 2;
            }
            else
            {
                slot_index += 1;
            }
        }
        func->varargs_index = slot_index;
    }

    /* Create fresh CodeBuilder for new function (pure factory pattern) */
    if (v->builder)
    {
        codebuilder_destroy(v->builder);
    }
    v->builder = codebuilder_create(code_output_cp(v->output), code_output_method(v->output),
                                    true, v->current_class_name,
                                    func ? func->param : NULL,
                                    func ? func->name : "<clinit>");

    /* Add __varargs parameter slot for variadic functions */
    if (func && func->is_variadic)
    {
        CB_VerificationType varargs_type = cb_type_object("[Ljava/lang/Object;");
        codebuilder_set_param(v->builder, (uint16_t)func->varargs_index, varargs_type);
    }

    v->ctx.scope_depth = 0;
    v->ctx.if_depth = 0;
    v->ctx.for_depth = 0;
    v->ctx.switch_depth = 0;
    cg_clear_symbols(v);
    v->ctx.has_return = false;
    v->bytecode_count = 0;
    v->has_last_bytecode = false;

    /* Reset label registry for new function */
    v->ctx.label_count = 0;

    /* Heap-lift parameters that need it (e.g., &param is used in function body).
     * For each such parameter, wrap its value in a 1-element array. */
    if (func)
    {
        for (ParameterList *p = func->param; p && !p->is_ellipsis; p = p->next)
        {
            Declaration *decl = p->decl;
            if (!decl || !decl->needs_heap_lift)
            {
                continue;
            }

            int orig_slot = decl->index;
            TypeSpecifier *param_type = decl->type;

            /* Load original parameter value from its slot */
            if (cs_type_is_pointer(param_type) || cs_type_is_array(param_type) ||
                cs_type_is_basic_struct_or_union(param_type))
            {
                codebuilder_build_aload(v->builder, orig_slot);
            }
            else if (cs_type_is_double_exact(param_type))
            {
                codebuilder_build_dload(v->builder, orig_slot);
            }
            else if (cs_type_is_long_exact(param_type))
            {
                codebuilder_build_lload(v->builder, orig_slot);
            }
            else if (cs_type_is_float_exact(param_type))
            {
                codebuilder_build_fload(v->builder, orig_slot);
            }
            else
            {
                codebuilder_build_iload(v->builder, orig_slot);
            }
            /* Stack: [value] */

            /* Create 1-element array */
            codebuilder_build_iconst(v->builder, 1);
            if (cs_type_is_pointer(param_type) || cs_type_is_array(param_type) ||
                cs_type_is_basic_struct_or_union(param_type))
            {
                int obj_class = cg_find_or_add_object_class(v);
                codebuilder_build_anewarray(v->builder, obj_class);
            }
            else
            {
                codebuilder_build_newarray(v->builder, newarray_type_code(param_type));
            }
            /* Stack: [value, array] */

            /* Store value into array[0] */
            codebuilder_build_dup_x1(v->builder);
            /* Stack: [array, value, array] */
            codebuilder_build_swap(v->builder);
            /* Stack: [array, array, value] */
            codebuilder_build_iconst(v->builder, 0);
            /* Stack: [array, array, value, 0] */
            codebuilder_build_swap(v->builder);
            /* Stack: [array, array, 0, value] */

            if (cs_type_is_pointer(param_type) || cs_type_is_array(param_type) ||
                cs_type_is_basic_struct_or_union(param_type))
            {
                codebuilder_build_aastore(v->builder);
            }
            else if (cs_type_is_double_exact(param_type))
            {
                codebuilder_build_dastore(v->builder);
            }
            else if (cs_type_is_long_exact(param_type))
            {
                codebuilder_build_lastore(v->builder);
            }
            else if (cs_type_is_float_exact(param_type))
            {
                codebuilder_build_fastore(v->builder);
            }
            else if (cs_type_is_char_exact(param_type) || cs_type_is_bool(param_type))
            {
                codebuilder_build_bastore(v->builder);
            }
            else if (cs_type_is_short_exact(param_type))
            {
                codebuilder_build_sastore(v->builder);
            }
            else
            {
                codebuilder_build_iastore(v->builder);
            }
            /* Stack: [array] */

            /* Allocate new local slot for the boxed array and store.
             * Array type depends on parameter type: primitives use [I, [B, etc.
             * References (pointers, arrays, structs) use [Ljava/lang/Object;
             *
             * NOTE: Do NOT call codebuilder_set_param here. The heap-lifted local
             * is NOT part of the method signature's initial frame (from JVM's perspective).
             * The JVM determines the initial frame from the method descriptor, so
             * adding locals here would cause StackMapTable verification errors.
             * The local is correctly tracked in frame (not initial_frame) by
             * codebuilder_allocate_local, so StackMapTable will use append_frame
             * to add it at the first branch target. */
            const char *array_desc = cg_heap_lift_array_descriptor(param_type);
            CB_VerificationType array_type = cb_type_object(array_desc);
            int new_slot = codebuilder_allocate_local(v->builder, array_type);
            codebuilder_build_astore(v->builder, new_slot);

            /* Update decl->index to point to the new boxed slot */
            decl->index = new_slot;
        }
    }
}

void codegen_finish_function(CodegenVisitor *v)
{
    /* Generate implicit return if:
     * - Function has no explicit return statement, OR
     * - Function has conditional returns but control flow can reach the end
     *   (builder->alive indicates we haven't hit an unconditional return/jump) */
    if (!v->ctx.has_return || v->builder->alive)
    {
        /* Get return type from function declaration
         * cminor_main now returns int (synthetic main wrapper handles the conversion) */
        TypeSpecifier *return_type = NULL;
        if (v->current_function)
        {
            return_type = v->current_function->type;
        }
        if (!return_type || cs_type_is_void(return_type))
        {
            if (v->builder->frame->stack_count > 0)
            {
                codebuilder_build_pop(v->builder);
            }
            codebuilder_build_return(v->builder);
        }
        else
        {
            if (v->builder->frame->stack_count == 0)
            {
                if (cs_type_is_pointer(return_type))
                {
                    /* Generate null pointer wrapper: __ptr(null, 0) */
                    codebuilder_build_aconst_null(v->builder);
                    codebuilder_build_iconst(v->builder, 0);
                    cg_emit_ptr_create(v, return_type);
                }
                else if (cs_type_is_aggregate(return_type))
                {
                    codebuilder_build_aconst_null(v->builder);
                }
                else if (cs_type_is_double_exact(return_type))
                {
                    codebuilder_build_dconst(v->builder, 0.0);
                }
                else if (cs_type_is_float_exact(return_type))
                {
                    codebuilder_build_fconst(v->builder, 0.0f);
                }
                else if (cs_type_is_long_exact(return_type))
                {
                    codebuilder_build_lconst(v->builder, 0);
                }
                else
                {
                    codebuilder_build_iconst(v->builder, 0);
                }
            }

            if (cs_type_is_aggregate(return_type) || cs_type_is_pointer(return_type) ||
                cs_type_is_array(return_type))
            {
                codebuilder_build_areturn(v->builder);
            }
            else if (cs_type_is_double_exact(return_type))
            {
                codebuilder_build_dreturn(v->builder);
            }
            else if (cs_type_is_float_exact(return_type))
            {
                codebuilder_build_freturn(v->builder);
            }
            else if (cs_type_is_long_exact(return_type))
            {
                codebuilder_build_lreturn(v->builder);
            }
            else if (cs_type_is_int_exact(return_type) || cs_type_is_short_exact(return_type) ||
                     cs_type_is_char_exact(return_type) || cs_type_is_bool(return_type) ||
                     cs_type_is_enum(return_type))
            {
                codebuilder_build_ireturn(v->builder);
            }
            else
            {
                /* Named types (typedefs) that are not primitives use areturn */
                codebuilder_build_areturn(v->builder);
            }
        }
    }

    /* Resolve any pending jumps from Label API */
    codebuilder_resolve_jumps(v->builder);

    /* Generate StackMapTable frames from CodeBuilder's branch targets */
    int frame_count = 0;
    v->temp_stack_map_frames = codebuilder_generate_stackmap(v->builder, v->stackmap_cp,
                                                             &frame_count);
    v->temp_stack_map_frame_count = frame_count;

    cg_clear_symbols(v);
    v->ctx.scope_depth = 0;
    v->current_function = NULL;
}

CodegenVisitor *create_codegen_visitor(CS_Compiler *compiler, CS_Executable *exec,
                                       const char *class_name)
{
    if (!compiler || !exec)
    {
        fprintf(stderr, "Compiler or Executable is NULL\n");
        exit(1);
    }

    CodegenVisitor *visitor = (CodegenVisitor *)calloc(1, sizeof(CodegenVisitor));
    visitor->compiler = compiler;
    visitor->exec = exec;
    visitor->output = code_output_create();
    visitor->static_field_capacity = 0;
    visitor->function_capacity = 0;
    visitor->ctx.scope_depth = 0;
    visitor->builder = codebuilder_create(code_output_cp(visitor->output), code_output_method(visitor->output),
                                          true, NULL, NULL, NULL);
    visitor->ctx.symbol_stack = NULL;
    visitor->ctx.if_stack = NULL;
    visitor->ctx.if_depth = 0;
    visitor->ctx.if_capacity = 0;
    visitor->ctx.for_stack = NULL;
    visitor->ctx.for_depth = 0;
    visitor->ctx.for_capacity = 0;
    visitor->ctx.switch_stack = NULL;
    visitor->ctx.switch_depth = 0;
    visitor->ctx.switch_capacity = 0;
    visitor->current_function = NULL;
    visitor->current_class_name = class_name ? strdup(class_name) : NULL;
    visitor->bytecode = NULL;
    visitor->bytecode_count = 0;
    visitor->bytecode_capacity = 0;
    visitor->last_bytecode_index = 0;
    visitor->has_last_bytecode = false;

    /* StackMapTable constant pool (merged into final classfile later) */
    visitor->stackmap_cp = cf_cp_create();

    register_static_fields(visitor);
    register_struct_definitions(visitor);
    register_functions(visitor);

    /* Legacy function pointer arrays are no longer used.
     * Switch-based traversal (codegen_traverse_expr/stmt) is used instead. */

    return visitor;
}

/* Switch-based AST traversal for CodegenVisitor.
 * These functions replace function pointer dispatch with direct switch-case,
 * preparing for self-compilation to JVM (no function pointers needed). */

#include "codegenvisitor_expr_assign.h"
#include "codegenvisitor_expr_complex.h"
#include "codegenvisitor_expr_ops.h"
#include "codegenvisitor_expr_values.h"
#include "codegenvisitor_stmt_basic.h"
#include "codegenvisitor_stmt_control.h"
#include "codegenvisitor_stmt_decl.h"
#include "codegenvisitor_stmt_switch_jump.h"

void codegen_traverse_expr(Expression *expr, CodegenVisitor *cg);
void codegen_traverse_stmt(Statement *stmt, CodegenVisitor *cg);

static void codegen_enter_expr(Expression *expr, CodegenVisitor *cg)
{
    switch (expr->kind)
    {
    case INT_EXPRESSION:
    case UINT_EXPRESSION:
        enter_intexpr(expr, (Visitor *)cg);
        break;
    case LONG_EXPRESSION:
    case ULONG_EXPRESSION:
        enter_longexpr(expr, (Visitor *)cg);
        break;
    case FLOAT_EXPRESSION:
        enter_floatexpr(expr, (Visitor *)cg);
        break;
    case DOUBLE_EXPRESSION:
        enter_doubleexpr(expr, (Visitor *)cg);
        break;
    case BOOL_EXPRESSION:
        enter_boolexpr(expr, (Visitor *)cg);
        break;
    case NULL_EXPRESSION:
        enter_nullexpr(expr, (Visitor *)cg);
        break;
    case STRING_EXPRESSION:
        enter_stringexpr(expr, (Visitor *)cg);
        break;
    case INCREMENT_EXPRESSION:
    case DECREMENT_EXPRESSION:
        enter_incexpr(expr, (Visitor *)cg);
        break;
    case ASSIGN_EXPRESSION:
        enter_assignexpr(expr, (Visitor *)cg);
        break;
    case FUNCTION_CALL_EXPRESSION:
        enter_funccallexpr(expr, (Visitor *)cg);
        break;
    case INITIALIZER_LIST_EXPRESSION:
        enter_initializerlistexpr(expr, (Visitor *)cg);
        break;
    case ADDRESS_EXPRESSION:
        enter_addrexpr(expr, (Visitor *)cg);
        break;
    case SIZEOF_EXPRESSION:
        enter_sizeofexpr(expr, (Visitor *)cg);
        break;
    case IDENTIFIER_EXPRESSION:
        enter_identifierexpr(expr, (Visitor *)cg);
        break;
    /* All other expressions use noop enter handler */
    default:
        enter_noop_expr(expr, (Visitor *)cg);
        break;
    }
}

static void codegen_leave_expr(Expression *expr, CodegenVisitor *cg)
{
    switch (expr->kind)
    {
    case INT_EXPRESSION:
    case UINT_EXPRESSION:
        leave_intexpr(expr, (Visitor *)cg);
        break;
    case LONG_EXPRESSION:
    case ULONG_EXPRESSION:
        leave_longexpr(expr, (Visitor *)cg);
        break;
    case FLOAT_EXPRESSION:
        leave_floatexpr(expr, (Visitor *)cg);
        break;
    case DOUBLE_EXPRESSION:
        leave_doubleexpr(expr, (Visitor *)cg);
        break;
    case BOOL_EXPRESSION:
        leave_boolexpr(expr, (Visitor *)cg);
        break;
    case NULL_EXPRESSION:
        leave_nullexpr(expr, (Visitor *)cg);
        break;
    case STRING_EXPRESSION:
        leave_stringexpr(expr, (Visitor *)cg);
        break;
    case IDENTIFIER_EXPRESSION:
        leave_identifierexpr(expr, (Visitor *)cg);
        break;
    case INCREMENT_EXPRESSION:
    case DECREMENT_EXPRESSION:
        leave_incexpr(expr, (Visitor *)cg);
        break;
    case ASSIGN_EXPRESSION:
        leave_assignexpr(expr, (Visitor *)cg);
        break;
    case ARRAY_EXPRESSION:
        leave_arrayexpr(expr, (Visitor *)cg);
        break;
    case MEMBER_EXPRESSION:
        leave_memberexpr(expr, (Visitor *)cg);
        break;
    case FUNCTION_CALL_EXPRESSION:
        leave_funccallexpr(expr, (Visitor *)cg);
        break;
    case MINUS_EXPRESSION:
        leave_unary_minus_expr(expr, (Visitor *)cg);
        break;
    case PLUS_EXPRESSION:
        leave_unary_plus_expr(expr, (Visitor *)cg);
        break;
    case LOGICAL_NOT_EXPRESSION:
        leave_logical_not_expr(expr, (Visitor *)cg);
        break;
    case ADD_EXPRESSION:
        leave_addexpr(expr, (Visitor *)cg);
        break;
    case SUB_EXPRESSION:
        leave_subexpr(expr, (Visitor *)cg);
        break;
    case MUL_EXPRESSION:
        leave_mulexpr(expr, (Visitor *)cg);
        break;
    case DIV_EXPRESSION:
        leave_divexpr(expr, (Visitor *)cg);
        break;
    case MOD_EXPRESSION:
        leave_modexpr(expr, (Visitor *)cg);
        break;
    case BIT_AND_EXPRESSION:
        leave_bit_and_expr(expr, (Visitor *)cg);
        break;
    case BIT_OR_EXPRESSION:
        leave_bit_or_expr(expr, (Visitor *)cg);
        break;
    case BIT_XOR_EXPRESSION:
        leave_bit_xor_expr(expr, (Visitor *)cg);
        break;
    case LSHIFT_EXPRESSION:
        leave_lshift_expr(expr, (Visitor *)cg);
        break;
    case RSHIFT_EXPRESSION:
        leave_rshift_expr(expr, (Visitor *)cg);
        break;
    case BIT_NOT_EXPRESSION:
        leave_bit_not_expr(expr, (Visitor *)cg);
        break;
    case EQ_EXPRESSION:
    case NE_EXPRESSION:
    case LT_EXPRESSION:
    case LE_EXPRESSION:
    case GT_EXPRESSION:
    case GE_EXPRESSION:
        leave_compareexpr(expr, (Visitor *)cg);
        break;
    case INITIALIZER_LIST_EXPRESSION:
        leave_initializerlistexpr(expr, (Visitor *)cg);
        break;
    case ADDRESS_EXPRESSION:
        leave_addrexpr(expr, (Visitor *)cg);
        break;
    case DEREFERENCE_EXPRESSION:
        leave_derefexpr(expr, (Visitor *)cg);
        break;
    case CAST_EXPRESSION:
        leave_castexpr(expr, (Visitor *)cg);
        break;
    case TYPE_CAST_EXPRESSION:
        leave_typecastexpr(expr, (Visitor *)cg);
        break;
    case ARRAY_TO_POINTER_EXPRESSION:
        leave_array_to_pointer_expr(expr, (Visitor *)cg);
        break;
    case SIZEOF_EXPRESSION:
        leave_sizeofexpr(expr, (Visitor *)cg);
        break;
    case CONDITIONAL_EXPRESSION:
        leave_conditionalexpr(expr, (Visitor *)cg);
        break;
    case LOGICAL_AND_EXPRESSION:
        leave_logical_and_expr(expr, (Visitor *)cg);
        break;
    case LOGICAL_OR_EXPRESSION:
        leave_logical_or_expr(expr, (Visitor *)cg);
        break;
    /* All other expressions use noop leave handler */
    default:
        leave_noop_expr(expr, (Visitor *)cg);
        break;
    }
}

static void codegen_traverse_expr_children(Expression *expr, CodegenVisitor *cg)
{
    switch (expr->kind)
    {
    case STRING_EXPRESSION:
    case IDENTIFIER_EXPRESSION:
    case DOUBLE_EXPRESSION:
    case FLOAT_EXPRESSION:
    case LONG_EXPRESSION:
    case ULONG_EXPRESSION:
    case INT_EXPRESSION:
    case UINT_EXPRESSION:
    case BOOL_EXPRESSION:
    case NULL_EXPRESSION:
        break;
    case ARRAY_EXPRESSION:
        codegen_traverse_expr(expr->u.array_expression.array, cg);
        codegen_traverse_expr(expr->u.array_expression.index, cg);
        break;
    case MEMBER_EXPRESSION:
        codegen_traverse_expr(expr->u.member_expression.target, cg);
        break;
    case CONDITIONAL_EXPRESSION:
        /* Traverse is handled in leave_conditionalexpr with control flow */
        break;
    case LOGICAL_AND_EXPRESSION:
    case LOGICAL_OR_EXPRESSION:
        /* Traverse is handled in leave handlers with short-circuit evaluation */
        break;
    case COMMA_EXPRESSION:
        codegen_traverse_expr(expr->u.comma_expression.left, cg);
        /* Pop left's result - comma discards it */
        if (cg->builder->frame->stack_count > 0)
        {
            codebuilder_build_pop_value(cg->builder);
        }
        codegen_traverse_expr(expr->u.comma_expression.right, cg);
        break;
    case INITIALIZER_LIST_EXPRESSION:
    {
        for (ExpressionList *p = expr->u.initializer_list; p; p = p->next)
        {
            codegen_traverse_expr(p->expression, cg);
        }
        break;
    }
    case DESIGNATED_INITIALIZER_EXPRESSION:
        codegen_traverse_expr(expr->u.designated_initializer.value, cg);
        break;
    case INCREMENT_EXPRESSION:
    case DECREMENT_EXPRESSION:
        codegen_traverse_expr(expr->u.inc_dec.target, cg);
        break;
    case MINUS_EXPRESSION:
        codegen_traverse_expr(expr->u.minus_expression, cg);
        break;
    case PLUS_EXPRESSION:
        codegen_traverse_expr(expr->u.plus_expression, cg);
        break;
    case LOGICAL_NOT_EXPRESSION:
        codegen_traverse_expr(expr->u.logical_not_expression, cg);
        break;
    case BIT_NOT_EXPRESSION:
        codegen_traverse_expr(expr->u.bit_not_expression, cg);
        break;
    case ADDRESS_EXPRESSION:
        codegen_traverse_expr(expr->u.address_expression, cg);
        break;
    case DEREFERENCE_EXPRESSION:
        codegen_traverse_expr(expr->u.dereference_expression, cg);
        break;
    case ASSIGN_EXPRESSION:
        codegen_traverse_expr(expr->u.assignment_expression.left, cg);
        /* No notify handler in codegen (all NULL) */
        codegen_traverse_expr(expr->u.assignment_expression.right, cg);
        break;
    case CAST_EXPRESSION:
        codegen_traverse_expr(expr->u.cast_expression.expr, cg);
        break;
    case TYPE_CAST_EXPRESSION:
        codegen_traverse_expr(expr->u.type_cast_expression.expr, cg);
        break;
    case SIZEOF_EXPRESSION:
        /* Don't traverse inner expression - sizeof just emits a constant */
        break;
    case ARRAY_TO_POINTER_EXPRESSION:
        codegen_traverse_expr(expr->u.array_to_pointer, cg);
        break;
    case FUNCTION_CALL_EXPRESSION:
    {
        ArgumentList *args = expr->u.function_call_expression.argument;
        for (; args; args = args->next)
        {
            codegen_traverse_expr(args->expr, cg);
        }
        codegen_traverse_expr(expr->u.function_call_expression.function, cg);
        break;
    }
    case LT_EXPRESSION:
    case LE_EXPRESSION:
    case GT_EXPRESSION:
    case GE_EXPRESSION:
    case EQ_EXPRESSION:
    case NE_EXPRESSION:
    case LSHIFT_EXPRESSION:
    case RSHIFT_EXPRESSION:
    case BIT_AND_EXPRESSION:
    case BIT_XOR_EXPRESSION:
    case BIT_OR_EXPRESSION:
    case MOD_EXPRESSION:
    case DIV_EXPRESSION:
    case MUL_EXPRESSION:
    case SUB_EXPRESSION:
    case ADD_EXPRESSION:
        if (expr->u.binary_expression.left)
        {
            codegen_traverse_expr(expr->u.binary_expression.left, cg);
        }
        if (expr->u.binary_expression.right)
        {
            codegen_traverse_expr(expr->u.binary_expression.right, cg);
        }
        break;
    default:
        break;
    }
}

void codegen_traverse_expr(Expression *expr, CodegenVisitor *cg)
{
    if (expr)
    {
        codegen_enter_expr(expr, cg);
        codegen_traverse_expr_children(expr, cg);
        codegen_leave_expr(expr, cg);
    }
}

static void codegen_enter_stmt(Statement *stmt, CodegenVisitor *cg)
{
    switch (stmt->type)
    {
    case COMPOUND_STATEMENT:
        enter_compound_stmt(stmt, (Visitor *)cg);
        break;
    case IF_STATEMENT:
        enter_ifstmt(stmt, (Visitor *)cg);
        break;
    case WHILE_STATEMENT:
        enter_whilestmt(stmt, (Visitor *)cg);
        break;
    case DO_WHILE_STATEMENT:
        enter_dowhilestmt(stmt, (Visitor *)cg);
        break;
    case FOR_STATEMENT:
        enter_forstmt(stmt, (Visitor *)cg);
        break;
    case SWITCH_STATEMENT:
        enter_switchstmt(stmt, (Visitor *)cg);
        break;
    case CASE_STATEMENT:
        enter_casestmt(stmt, (Visitor *)cg);
        break;
    case DEFAULT_STATEMENT:
        enter_defaultstmt(stmt, (Visitor *)cg);
        break;
    case LABEL_STATEMENT:
        enter_labelstmt(stmt, (Visitor *)cg);
        break;
    /* All other statements use generic enter handler */
    default:
        enter_generic_stmt(stmt, (Visitor *)cg);
        break;
    }
}

static void codegen_leave_stmt(Statement *stmt, CodegenVisitor *cg)
{
    switch (stmt->type)
    {
    case COMPOUND_STATEMENT:
        leave_compound_stmt(stmt, (Visitor *)cg);
        break;
    case IF_STATEMENT:
        leave_ifstmt(stmt, (Visitor *)cg);
        break;
    case WHILE_STATEMENT:
        leave_whilestmt(stmt, (Visitor *)cg);
        break;
    case DO_WHILE_STATEMENT:
        leave_dowhilestmt(stmt, (Visitor *)cg);
        break;
    case FOR_STATEMENT:
        leave_forstmt(stmt, (Visitor *)cg);
        break;
    case SWITCH_STATEMENT:
        leave_switchstmt(stmt, (Visitor *)cg);
        break;
    case CASE_STATEMENT:
        leave_casestmt(stmt, (Visitor *)cg);
        break;
    case DEFAULT_STATEMENT:
        leave_defaultstmt(stmt, (Visitor *)cg);
        break;
    case BREAK_STATEMENT:
        leave_breakstmt(stmt, (Visitor *)cg);
        break;
    case CONTINUE_STATEMENT:
        leave_continuestmt(stmt, (Visitor *)cg);
        break;
    case EXPRESSION_STATEMENT:
        leave_exprstmt(stmt, (Visitor *)cg);
        break;
    case DECLARATION_STATEMENT:
        leave_declstmt(stmt, (Visitor *)cg);
        break;
    case RETURN_STATEMENT:
        leave_returnstmt(stmt, (Visitor *)cg);
        break;
    case GOTO_STATEMENT:
        leave_gotostmt(stmt, (Visitor *)cg);
        break;
    case LABEL_STATEMENT:
        leave_labelstmt(stmt, (Visitor *)cg);
        break;
    /* All other statements use generic leave handler */
    default:
        leave_generic_stmt(stmt, (Visitor *)cg);
        break;
    }
}

static void codegen_traverse_stmt_children(Statement *stmt, CodegenVisitor *cg)
{
    switch (stmt->type)
    {
    case EXPRESSION_STATEMENT:
        /* Reachability is checked at codegen_traverse_stmt level */
        codegen_traverse_expr(stmt->u.expression_s, cg);
        break;
    case DECLARATION_STATEMENT:
    {
        /* Reachability is checked at codegen_traverse_stmt level */
        Declaration *decl = stmt->u.declaration_s;
        if (decl && decl->type && cs_type_is_array(decl->type))
        {
            for (TypeSpecifier *t = decl->type; t && cs_type_is_array(t); t = cs_type_child(t))
            {
                Expression *size_expr = cs_type_array_size(t);
                if (size_expr && size_expr->kind != INT_EXPRESSION &&
                    size_expr->kind != BOOL_EXPRESSION)
                {
                    codegen_traverse_expr(size_expr, cg);
                }
            }
        }
        codegen_traverse_expr(decl ? decl->initializer : NULL, cg);
        break;
    }
    case COMPOUND_STATEMENT:
    {
        for (StatementList *p = stmt->u.compound_s.list; p; p = p->next)
        {
            codegen_traverse_stmt(p->stmt, cg);
        }
        break;
    }
    case IF_STATEMENT:
        /* Only evaluate condition if reachable */
        if (cg->builder->alive)
        {
            codegen_traverse_expr(stmt->u.if_s.condition, cg);
        }
        codegen_traverse_stmt(stmt->u.if_s.then_statement, cg);
        codegen_traverse_stmt(stmt->u.if_s.else_statement, cg);
        break;
    case WHILE_STATEMENT:
        /* Only evaluate condition if reachable */
        if (cg->builder->alive)
        {
            codegen_traverse_expr(stmt->u.while_s.condition, cg);
        }
        codegen_traverse_stmt(stmt->u.while_s.body, cg);
        break;
    case DO_WHILE_STATEMENT:
        codegen_traverse_stmt(stmt->u.do_s.body, cg);
        /* Only generate condition code if body is reachable */
        if (cg->builder->alive)
        {
            codebuilder_do_while_cond(cg->builder);
            codegen_traverse_expr(stmt->u.do_s.condition, cg);
        }
        break;
    case FOR_STATEMENT:
        /* Only evaluate init/condition/post if reachable */
        if (cg->builder->alive)
        {
            codegen_traverse_stmt(stmt->u.for_s.init, cg);
            codegen_traverse_expr(stmt->u.for_s.condition, cg);
            /* If body is NULL (empty for loop like "for(...);"), we still need to
             * generate the condition branch. handle_for_body_entry is normally
             * called when entering the body statement, but with NULL body it's
             * never called, leaving the condition value on the stack. */
            if (!stmt->u.for_s.body)
            {
                handle_for_body_entry(cg, NULL);
            }
        }
        codegen_traverse_stmt(stmt->u.for_s.body, cg);
        if (cg->builder->alive)
        {
            codegen_traverse_expr(stmt->u.for_s.post, cg);
        }
        break;
    case SWITCH_STATEMENT:
        /* Only evaluate expression if reachable */
        if (cg->builder->alive)
        {
            codegen_traverse_expr(stmt->u.switch_s.expression, cg);
        }
        codegen_traverse_stmt(stmt->u.switch_s.body, cg);
        break;
    case CASE_STATEMENT:
        codegen_traverse_stmt(stmt->u.case_s.statement, cg);
        break;
    case DEFAULT_STATEMENT:
        codegen_traverse_stmt(stmt->u.default_s.statement, cg);
        break;
    case LABEL_STATEMENT:
        codegen_traverse_stmt(stmt->u.label_s.statement, cg);
        break;
    case RETURN_STATEMENT:
        codegen_traverse_expr(stmt->u.return_s.expression, cg);
        break;
    case GOTO_STATEMENT:
    case BREAK_STATEMENT:
    case CONTINUE_STATEMENT:
        break;
    default:
        break;
    }
}

void codegen_traverse_stmt(Statement *stmt, CodegenVisitor *cg)
{
    if (!stmt)
    {
        return;
    }

    /*
     * Javac-style reachability gate:
     * Skip code generation for unreachable statements, EXCEPT for:
     * - Label statements (can revive reachability via jumps)
     * - Case/Default statements (can be reached via switch dispatch)
     * - Compound statements (may contain reachable labels inside)
     * - Control structures (if/while/for/switch/do-while) - they create labels
     *   that must be placed even if dead, and may contain reachable labels
     */
    if (!codebuilder_is_alive(cg->builder))
    {
        switch (stmt->type)
        {
        case LABEL_STATEMENT:
        case CASE_STATEMENT:
        case DEFAULT_STATEMENT:
        case COMPOUND_STATEMENT:
        case IF_STATEMENT:
        case WHILE_STATEMENT:
        case DO_WHILE_STATEMENT:
        case FOR_STATEMENT:
        case SWITCH_STATEMENT:
            /* These may revive reachability or create labels - process them */
            break;
        default:
            /* Dead code - skip generation */
            return;
        }
    }

    /* Record line number for debugging (LineNumberTable) */
    if (stmt->line_number > 0 && codebuilder_is_alive(cg->builder))
    {
        method_code_add_line_number(code_output_method(cg->output), stmt->line_number);
    }

    codegen_enter_stmt(stmt, cg);
    codegen_traverse_stmt_children(stmt, cg);
    codegen_leave_stmt(stmt, cg);
}
