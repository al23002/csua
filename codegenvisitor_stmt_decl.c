#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codegenvisitor_stmt_decl.h"
#include "codegenvisitor.h"
#include "codebuilder_ptr.h"
#include "codegenvisitor_util.h"
#include "codegenvisitor_stmt_util.h"
#include "codebuilder_label.h"
#include "codebuilder_part1.h"
#include "codebuilder_part2.h"
#include "codebuilder_part3.h"
#include "codegen_symbols.h"
#include "codegen_constants.h"
#include "codegen_jvm_types.h"
#include "cminor_type.h"
#include "synthetic_codegen.h"

/* Helper to find field index by name in a class definition */
static int find_field_index_by_name(CG_ClassDef *class_def, const char *field_name)
{
    for (int i = 0; i < class_def->field_count; i++)
    {
        if (class_def->fields[i].name && field_name &&
            strcmp(class_def->fields[i].name, field_name) == 0)
        {
            return (int)i;
        }
    }
    return -1; /* Not found */
}

void leave_declstmt(Statement *stmt, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;

    /* Skip code generation for unreachable declarations.
     * This prevents generating bytecode after goto/return. */
    if (!cg->builder->alive)
    {
        return;
    }

    Declaration *decl = stmt->u.declaration_s;
    if (!decl)
    {
        /* No scope cleanup (block-level scoping) */
        return;
    }

    CodegenSymbolInfo sym = cg_ensure_symbol(cg, decl);

    /* Handle arrays (both VLA and fixed-size).
     * VLA: sizes are on stack from traversor (innermost on top)
     * Fixed: sizes are compile-time constants
     * Arrays with initializer lists are handled by leave_initializerlistexpr. */
    bool is_vla = is_vla_type(decl->type);
    bool is_fixed_array = decl->type && cs_type_is_array(decl->type) && !decl->initializer && !is_vla;

    if (is_vla || is_fixed_array)
    {
        int dims = count_array_dimensions(decl->type);
        TypeSpecifier *element_type = array_element_type(decl->type);

        if (dims > 3)
        {
            fprintf(stderr, "error: 4D+ arrays not supported (dims=%u)\n", dims);
            exit(1);
        }
        if (!element_type)
        {
            fprintf(stderr, "error: array element type missing\n");
            exit(1);
        }

        /* Allocate locals for dimension sizes (dim_locals[0] = outermost) */
        int dim_locals[3];
        for (int i = 0; i < dims; i++)
        {
            dim_locals[i] = allocate_temp_local(cg);
        }

        /* Store dimension sizes into locals.
         * VLA: pop from stack (innermost first, so store in reverse)
         * Fixed: push constants (outermost first) */
        if (is_vla)
        {
            for (int i = dims - 1; i >= 0; i--)
            {
                codebuilder_build_istore(cg->builder, dim_locals[i]);
            }
        }
        else
        {
            TypeSpecifier *t = decl->type;
            for (int i = 0; i < dims; i++)
            {
                int len = array_length_from_type(t);
                codebuilder_build_iconst(cg->builder, (int32_t)len);
                codebuilder_build_istore(cg->builder, dim_locals[i]);
                t = cs_type_child(t);
            }
        }

        /* 1D: create array with appropriate type */
        if (dims == 1)
        {
            codebuilder_build_iload(cg->builder, dim_locals[0]);
            /* Use anewarray for struct/pointer types, newarray for primitives */
            if (element_type && cs_type_is_basic_struct_or_union(element_type))
            {
                const char *struct_name = cs_type_user_type_name(element_type);
                int struct_class_idx = cg_find_or_add_class(cg, struct_name, -1);
                codebuilder_build_anewarray(cg->builder, struct_class_idx);
                codebuilder_build_astore(cg->builder, sym.index);

                /* Initialize each element: for (i = 0; i < dim; i++) arr[i] = new T(); */
                int idx_local = allocate_temp_local(cg);
                codebuilder_build_iconst(cg->builder, 0);
                codebuilder_build_istore(cg->builder, idx_local);

                CB_Label *init_loop_cond = codebuilder_create_label(cg->builder);
                CB_Label *init_loop_end = codebuilder_create_label(cg->builder);
                codebuilder_mark_loop_header(cg->builder, init_loop_cond);
                codebuilder_place_label(cg->builder, init_loop_cond);

                codebuilder_build_iload(cg->builder, idx_local);
                codebuilder_build_iload(cg->builder, dim_locals[0]);
                codebuilder_jump_if_icmp(cg->builder, ICMP_GE, init_loop_end);

                /* arr[i] = new T() */
                codebuilder_build_aload(cg->builder, sym.index);
                codebuilder_build_iload(cg->builder, idx_local);
                codebuilder_build_new(cg->builder, struct_class_idx);
                codebuilder_build_dup(cg->builder);
                int init_idx = cp_builder_add_methodref(code_output_cp(cg->output),
                                                        struct_name, "<init>", "()V");
                codebuilder_build_invokespecial(cg->builder, init_idx);
                codebuilder_build_aastore(cg->builder);

                codebuilder_build_iinc(cg->builder, idx_local, 1);
                codebuilder_jump(cg->builder, init_loop_cond);

                codebuilder_place_label(cg->builder, init_loop_end);
            }
            else if (element_type && cs_type_is_pointer(element_type))
            {
                PtrTypeIndex ptr_kind = (PtrTypeIndex)cg_pointer_runtime_kind(element_type);
                const char *ptr_class_name = ptr_type_class_name(ptr_kind);
                int ptr_class_idx = cg_find_or_add_class(cg, ptr_class_name, -1);
                codebuilder_build_anewarray(cg->builder, ptr_class_idx);
                codebuilder_build_astore(cg->builder, sym.index);

                /* Initialize each element: for (i = 0; i < dim; i++) arr[i] = new PtrT(); */
                int idx_local = allocate_temp_local(cg);
                codebuilder_build_iconst(cg->builder, 0);
                codebuilder_build_istore(cg->builder, idx_local);

                CB_Label *init_loop_cond = codebuilder_create_label(cg->builder);
                CB_Label *init_loop_end = codebuilder_create_label(cg->builder);
                codebuilder_mark_loop_header(cg->builder, init_loop_cond);
                codebuilder_place_label(cg->builder, init_loop_cond);

                codebuilder_build_iload(cg->builder, idx_local);
                codebuilder_build_iload(cg->builder, dim_locals[0]);
                codebuilder_jump_if_icmp(cg->builder, ICMP_GE, init_loop_end);

                /* arr[i] = new PtrT() */
                codebuilder_build_aload(cg->builder, sym.index);
                codebuilder_build_iload(cg->builder, idx_local);
                codebuilder_build_new(cg->builder, ptr_class_idx);
                codebuilder_build_dup(cg->builder);
                int init_idx = cp_builder_add_methodref(code_output_cp(cg->output),
                                                        ptr_class_name, "<init>", "()V");
                codebuilder_build_invokespecial(cg->builder, init_idx);
                codebuilder_build_aastore(cg->builder);

                codebuilder_build_iinc(cg->builder, idx_local, 1);
                codebuilder_jump(cg->builder, init_loop_cond);

                codebuilder_place_label(cg->builder, init_loop_end);
            }
            else
            {
                codebuilder_build_newarray(cg->builder, newarray_type_code(element_type));
                codebuilder_build_astore(cg->builder, sym.index);
            }
            return;
        }

        /* 2D+: create array of arrays using loops */
        TypeSpecifier *child_type = element_type; /* e.g., int[] for int[][] */
        TypeSpecifier *inner_elem_type = child_type ? array_element_type(child_type) : NULL;
        if (!child_type || !cs_type_is_array(child_type) || !inner_elem_type)
        {
            fprintf(stderr, "error: nested array element type missing\n");
            exit(1);
        }

        /* Create outermost array */
        codebuilder_build_iload(cg->builder, dim_locals[0]);
        int child_class_idx = cg_find_or_add_array_class(cg, child_type);
        codebuilder_build_anewarray(cg->builder, child_class_idx);
        codebuilder_build_astore(cg->builder, sym.index);

        /* Allocate loop index locals and initialize all before first loop header (for StackMap) */
        int idx_locals[2]; /* max 2 loop levels for 3D */
        int num_loops = dims - 1;
        for (int i = 0; i < num_loops; i++)
        {
            idx_locals[i] = allocate_temp_local(cg);
            codebuilder_build_iconst(cg->builder, 0);
            codebuilder_build_istore(cg->builder, idx_locals[i]);
        }

        /* Outer loop: for (i = 0; i < dim[0]; i++) */
        CB_Label *outer_cond = codebuilder_create_label(cg->builder);
        CB_Label *outer_end = codebuilder_create_label(cg->builder);
        codebuilder_mark_loop_header(cg->builder, outer_cond);
        codebuilder_place_label(cg->builder, outer_cond);

        codebuilder_build_iload(cg->builder, idx_locals[0]);
        codebuilder_build_iload(cg->builder, dim_locals[0]);
        codebuilder_jump_if_icmp(cg->builder, ICMP_GE, outer_end);

        if (dims == 2)
        {
            /* arr[i] = new T[dim[1]] */
            codebuilder_build_aload(cg->builder, sym.index);
            codebuilder_build_iload(cg->builder, idx_locals[0]);
            codebuilder_build_iload(cg->builder, dim_locals[1]);
            /* Use anewarray for struct/pointer types, newarray for primitives */
            if (inner_elem_type && cs_type_is_basic_struct_or_union(inner_elem_type))
            {
                const char *struct_name = cs_type_user_type_name(inner_elem_type);
                int struct_class_idx = cg_find_or_add_class(cg, struct_name, -1);
                codebuilder_build_anewarray(cg->builder, struct_class_idx);
            }
            else if (inner_elem_type && cs_type_is_pointer(inner_elem_type))
            {
                PtrTypeIndex ptr_kind = (PtrTypeIndex)cg_pointer_runtime_kind(inner_elem_type);
                const char *ptr_class_name = ptr_type_class_name(ptr_kind);
                int ptr_class_idx = cg_find_or_add_class(cg, ptr_class_name, -1);
                codebuilder_build_anewarray(cg->builder, ptr_class_idx);
            }
            else
            {
                codebuilder_build_newarray(cg->builder, newarray_type_code(inner_elem_type));
            }
            codebuilder_build_aastore(cg->builder);
        }
        else /* dims == 3 */
        {
            /* arr[i] = new T[][dim[1]] */
            TypeSpecifier *inner_type = child_type ? array_element_type(child_type) : NULL; /* int[] */
            TypeSpecifier *base_type = inner_type ? array_element_type(inner_type) : NULL;
            if (!inner_type || !base_type)
            {
                fprintf(stderr, "error: nested array element type missing\n");
                exit(1);
            }
            codebuilder_build_aload(cg->builder, sym.index);
            codebuilder_build_iload(cg->builder, idx_locals[0]);
            codebuilder_build_iload(cg->builder, dim_locals[1]);
            int inner_class_idx = cg_find_or_add_array_class(cg, inner_type);
            codebuilder_build_anewarray(cg->builder, inner_class_idx);
            codebuilder_build_aastore(cg->builder);

            /* Inner loop: for (j = 0; j < dim[1]; j++) */
            codebuilder_build_iconst(cg->builder, 0);
            codebuilder_build_istore(cg->builder, idx_locals[1]);

            CB_Label *inner_cond = codebuilder_create_label(cg->builder);
            CB_Label *inner_end = codebuilder_create_label(cg->builder);
            codebuilder_mark_loop_header(cg->builder, inner_cond);
            codebuilder_place_label(cg->builder, inner_cond);

            codebuilder_build_iload(cg->builder, idx_locals[1]);
            codebuilder_build_iload(cg->builder, dim_locals[1]);
            codebuilder_jump_if_icmp(cg->builder, ICMP_GE, inner_end);

            /* arr[i][j] = new T[dim[2]] */
            codebuilder_build_aload(cg->builder, sym.index);
            codebuilder_build_iload(cg->builder, idx_locals[0]);
            codebuilder_build_aaload(cg->builder);
            codebuilder_build_iload(cg->builder, idx_locals[1]);
            codebuilder_build_iload(cg->builder, dim_locals[2]);
            /* Use anewarray for struct/pointer types, newarray for primitives */
            if (base_type && cs_type_is_basic_struct_or_union(base_type))
            {
                const char *struct_name = cs_type_user_type_name(base_type);
                int struct_class_idx = cg_find_or_add_class(cg, struct_name, -1);
                codebuilder_build_anewarray(cg->builder, struct_class_idx);
            }
            else if (base_type && cs_type_is_pointer(base_type))
            {
                PtrTypeIndex ptr_kind = (PtrTypeIndex)cg_pointer_runtime_kind(base_type);
                const char *ptr_class_name = ptr_type_class_name(ptr_kind);
                int ptr_class_idx = cg_find_or_add_class(cg, ptr_class_name, -1);
                codebuilder_build_anewarray(cg->builder, ptr_class_idx);
            }
            else
            {
                codebuilder_build_newarray(cg->builder, newarray_type_code(base_type));
            }
            codebuilder_build_aastore(cg->builder);

            codebuilder_build_iinc(cg->builder, idx_locals[1], 1);
            codebuilder_jump(cg->builder, inner_cond);

            codebuilder_place_label(cg->builder, inner_end);
        }

        codebuilder_build_iinc(cg->builder, idx_locals[0], 1);
        codebuilder_jump(cg->builder, outer_cond);

        codebuilder_place_label(cg->builder, outer_end);
        return;
    }

    /* Handle struct/union type: allocate object with CF_NEW.
     * Skip enum types and typedef aliases for primitive types (e.g., uint32_t). */
    const char *struct_name = cs_type_user_type_name(decl->type);
    if (struct_name && cs_type_is_named(decl->type) && cs_type_is_basic_struct_or_union(decl->type))
    {
        if (decl->initializer &&
            decl->initializer->kind != INITIALIZER_LIST_EXPRESSION)
        {
            /* Initializer is an expression (e.g., variable or function call).
             * For C value semantics, struct assignment creates a copy.
             * Stack: [src_ref] -> [new_ref] after deep copy */
            cg_emit_struct_deep_copy(cg, decl->type);
        }
        else if (decl->initializer &&
                 decl->initializer->kind == INITIALIZER_LIST_EXPRESSION)
        {
            /* Initializer list: Stack has [val_0, val_1, ..., val_n-1] */
            ExpressionList *init_list = decl->initializer->u.initializer_list;

            /* Count initializers */
            int init_count = 0;
            for (ExpressionList *p = init_list; p; p = p->next)
                init_count++;

            /* Build field index array for designated initializers */
            int *field_indices = NULL;
            if (init_list && init_list->expression &&
                init_list->expression->kind == DESIGNATED_INITIALIZER_EXPRESSION)
            {
                int class_idx = find_class_index(cg, struct_name);
                CG_ClassDef *class_def = &cg->class_defs[class_idx];

                field_indices = (int *)calloc(init_count, sizeof(int));
                int idx = 0;
                for (ExpressionList *p = init_list; p; p = p->next, idx++)
                {
                    Expression *di = p->expression;
                    if (di && di->kind == DESIGNATED_INITIALIZER_EXPRESSION)
                    {
                        const char *fname = di->u.designated_initializer.field_name;
                        field_indices[idx] = find_field_index_by_name(class_def, fname);
                    }
                    else
                    {
                        field_indices[idx] = -1;
                    }
                }
            }

            /* Extract type info for each field value for array-to-pointer conversion */
            TypeSpecifier **value_types = NULL;
            if (init_count > 0)
            {
                value_types = (TypeSpecifier **)calloc(init_count, sizeof(TypeSpecifier *));
                int vi = 0;
                for (ExpressionList *p = init_list; p; p = p->next, vi++)
                {
                    if (p->expression)
                        value_types[vi] = p->expression->type;
                }
            }

            /* Use common function for both positional and designated */
            cg_emit_struct_from_init_values(cg, struct_name, field_indices, init_count, value_types);

            if (field_indices)
                free(field_indices);
            if (value_types)
                free(value_types);
        }
        else
        {
            /* No initializer: allocate new struct with recursive embedded struct init */
            cg_emit_struct_from_init_values(cg, struct_name, NULL, 0, NULL);
        }

        /* Stack: [struct_obj]
         * If heap-lift is needed, box the struct into Object[1] array */
        if (decl->needs_heap_lift)
        {
            /* Stack: [struct_obj] */
            int obj_class = cg_find_or_add_object_class(cg);
            codebuilder_build_iconst(cg->builder, 1);
            codebuilder_build_anewarray(cg->builder, obj_class);
            /* Stack: [struct_obj, array] */
            codebuilder_build_dup_x1(cg->builder);
            /* Stack: [array, struct_obj, array] */
            codebuilder_build_swap(cg->builder);
            /* Stack: [array, array, struct_obj] */
            codebuilder_build_iconst(cg->builder, 0);
            /* Stack: [array, array, struct_obj, 0] */
            codebuilder_build_swap(cg->builder);
            /* Stack: [array, array, 0, struct_obj] */
            codebuilder_build_aastore(cg->builder);
            /* Stack: [array] */
        }

        codebuilder_build_astore(cg->builder, sym.index);
        /* No scope cleanup (block-level scoping) */
        return;
    }

    /* Handle heap-lifted variables: box into 1-element array.
     * This allows taking the address of local variables on JVM. */
    if (decl->needs_heap_lift)
    {
        /* Create 1-element array to box the value.
         * For pointer/array/struct types, use ANEWARRAY with Object.
         * For primitive types, use NEWARRAY with the appropriate type code. */
        codebuilder_build_iconst(cg->builder, 1);
        if (cs_type_is_pointer(decl->type) || cs_type_is_array(decl->type) ||
            cs_type_is_basic_struct_or_union(decl->type))
        {
            int obj_class = cg_find_or_add_object_class(cg);
            codebuilder_build_anewarray(cg->builder, obj_class);
        }
        else
        {
            codebuilder_build_newarray(cg->builder, newarray_type_code(decl->type));
        }

        if (decl->initializer)
        {
            /* Stack: [init_value, array_ref]
             * Need: array_ref on stack, then store init_value at index 0
             * Use semantic API that auto-selects dup_x1 or dup2_x1 based on init_value category */
            codebuilder_build_dup_value_x1(cg->builder);
            /* Stack: [array_ref, init_value, array_ref] */
            codebuilder_build_swap(cg->builder);
            /* Stack: [array_ref, array_ref, init_value] */
            codebuilder_build_iconst(cg->builder, 0);
            /* Stack: [array_ref, array_ref, init_value, 0] */
            codebuilder_build_swap(cg->builder);
            /* Stack: [array_ref, array_ref, 0, init_value] */
            if (decl->type && (cs_type_is_array(decl->type) ||
                               cs_type_is_pointer(decl->type) ||
                               cs_type_is_basic_struct_or_union(decl->type)))
            {
                codebuilder_build_aastore(cg->builder);
            }
            else if (decl->type && cs_type_is_double_exact(decl->type))
            {
                codebuilder_build_dastore(cg->builder);
            }
            else if (decl->type)
            {
                if (cs_type_is_char_exact(decl->type) || cs_type_is_bool(decl->type))
                {
                    codebuilder_build_bastore(cg->builder);
                }
                else if (cs_type_is_short_exact(decl->type))
                {
                    codebuilder_build_sastore(cg->builder);
                }
                else if (cs_type_is_long_exact(decl->type))
                {
                    codebuilder_build_lastore(cg->builder);
                }
                else if (cs_type_is_float_exact(decl->type))
                {
                    codebuilder_build_fastore(cg->builder);
                }
                else
                {
                    codebuilder_build_iastore(cg->builder);
                }
            }
            else
            {
                codebuilder_build_iastore(cg->builder);
            }
            /* Stack: [array_ref] */
        }

        /* Store array reference in local */
        codebuilder_build_astore(cg->builder, sym.index);
        /* No scope cleanup (block-level scoping) */
        return;
    }

    if (decl->initializer)
    {
        CF_ValueTag tag = cg_decl_value_tag(decl);
        if (sym.kind == CG_SYMBOL_STATIC)
        {
            int pool_idx = cg_find_or_add_field(cg, decl);
            codebuilder_build_putstatic(cg->builder, pool_idx);
        }
        else
        {
            /* For pointer types initialized from another pointer variable,
             * we need to clone the pointer wrapper to avoid Java reference aliasing.
             * In C: int *p = q; creates two independent pointers with same target.
             * In Java: __intPtr p = q; would make p and q the same object.
             * We need to create a new wrapper with the same base and offset.
             *
             * Only clone when initializer is a simple identifier (variable reference).
             * Function calls, array subscripts, etc. already return new objects. */
            bool needs_clone = false;
            if (cs_type_is_pointer(decl->type) && decl->initializer->type &&
                cs_type_is_pointer(decl->initializer->type))
            {
                Expression *init_expr = decl->initializer;
                /* Clone only for: identifier (variable reference) */
                if (init_expr->kind == IDENTIFIER_EXPRESSION)
                {
                    needs_clone = true;
                }
            }
            if (needs_clone)
            {
                cg_emit_ptr_clone(cg, decl->type);
                codebuilder_build_astore(cg->builder, sym.index);
            }
            else
            {
                switch (tag)
                {
                case CF_VAL_INT:
                    codebuilder_build_istore(cg->builder, sym.index);
                    break;
                case CF_VAL_LONG:
                    codebuilder_build_lstore(cg->builder, sym.index);
                    break;
                case CF_VAL_FLOAT:
                    codebuilder_build_fstore(cg->builder, sym.index);
                    break;
                case CF_VAL_DOUBLE:
                    codebuilder_build_dstore(cg->builder, sym.index);
                    break;
                case CF_VAL_OBJECT:
                case CF_VAL_NULL:
                    codebuilder_build_astore(cg->builder, sym.index);
                    break;
                default:
                    fprintf(stderr, "leave_declstmt: invalid tag %d for local %d\n", tag, sym.index);
                    exit(1);
                }
            }
        }
    }
    else if (decl->type && cs_type_is_pointer(decl->type) &&
             sym.kind != CG_SYMBOL_STATIC)
    {
        /* Uninitialized pointer variable: initialize to null pointer wrapper.
         * JVM requires all local variables to be definitely assigned before use.
         * Generate __ptr(null, 0) instead of raw aconst_null for type consistency. */
        codebuilder_build_aconst_null(cg->builder);
        codebuilder_build_iconst(cg->builder, 0);
        cg_emit_ptr_create(cg, decl->type);
        codebuilder_build_astore(cg->builder, sym.index);
    }
    else if (!decl->initializer && sym.kind == CG_SYMBOL_LOCAL)
    {
        /* Uninitialized scalar local variable: zero-initialize for JVM verification.
         * When goto jumps backward over uninitialized locals, the StackMapTable
         * expects all locals to have defined types. Without initialization,
         * the JVM verifier sees TOP (undefined) instead of the declared type. */
        CF_ValueTag tag = cg_decl_value_tag(decl);
        switch (tag)
        {
        case CF_VAL_INT:
            codebuilder_build_iconst(cg->builder, 0);
            codebuilder_build_istore(cg->builder, sym.index);
            break;
        case CF_VAL_LONG:
            codebuilder_build_lconst(cg->builder, 0);
            codebuilder_build_lstore(cg->builder, sym.index);
            break;
        case CF_VAL_FLOAT:
            codebuilder_build_fconst(cg->builder, 0.0f);
            codebuilder_build_fstore(cg->builder, sym.index);
            break;
        case CF_VAL_DOUBLE:
            codebuilder_build_dconst(cg->builder, 0.0);
            codebuilder_build_dstore(cg->builder, sym.index);
            break;
        default:
            /* Reference types: initialize to null */
            codebuilder_build_aconst_null(cg->builder);
            codebuilder_build_astore(cg->builder, sym.index);
            break;
        }
    }
    /* No scope cleanup (block-level scoping) */
}
