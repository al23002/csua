#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codegen_constants.h"
#include "codegen_jvm_types.h"
#include "codegen_symbols.h"
#include "codegenvisitor_expr_complex.h"
#include "codegenvisitor.h"
#include "codebuilder_ptr.h"
#include "codegenvisitor_util.h"
#include "codegenvisitor_expr_util.h"
#include "codebuilder_frame.h"
#include "codebuilder_label.h"
#include "codebuilder_part1.h"
#include "codebuilder_part2.h"
#include "codebuilder_part3.h"
#include "codebuilder_types.h"
#include "cminor_type.h"
#include "util.h"
#include "synthetic_codegen.h"

void enter_initializerlistexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    mark_for_condition_start(cg, expr);
    cg->ctx.flatten_init_depth++;
}

void leave_initializerlistexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    if (cg->ctx.flatten_init_depth == 0)
    {
        fprintf(stderr, "initializer list depth underflow\n");
        exit(1);
    }
    cg->ctx.flatten_init_depth--;

    TypeSpecifier *init_type = expr->type;

    /* Handle nested struct initializer list (e.g., inner {...} in Foo arr[] = {{...}, {...}}).
     * When depth > 0 and type is struct, create the struct object now. */
    if (cg->ctx.flatten_init_depth > 0)
    {
        if (init_type && cs_type_is_named(init_type) &&
            cs_type_is_basic_struct_or_union(init_type))
        {
            /* Nested struct initializer: create struct from values on stack */
            const char *struct_name = cs_type_user_type_name(init_type);
            ExpressionList *init_list = expr->u.initializer_list;
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
                        for (int fi = 0; fi < class_def->field_count; fi++)
                        {
                            if (class_def->fields[fi].name && fname &&
                                strcmp(class_def->fields[fi].name, fname) == 0)
                            {
                                field_indices[idx] = (int)fi;
                                break;
                            }
                        }
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

            cg_emit_struct_from_init_values(cg, struct_name, field_indices, init_count, value_types);
            if (field_indices)
                free(field_indices);
            if (value_types)
                free(value_types);
        }
        handle_for_expression_leave(cg, expr);
        return;
    }

    /* Handle struct initializer lists: values are already on stack.
     * leave_declstmt will pop them and assign to struct fields. */
    if (init_type && cs_type_is_named(init_type))
    {
        handle_for_expression_leave(cg, expr);
        return;
    }

    /* For arrays, create the array and populate it */
    if (!init_type || !cs_type_is_array(init_type))
    {
        fprintf(stderr, "initializer list requires array or struct type\n");
        exit(1);
    }

    TypeSpecifier *array_type = init_type;

    int value_count = count_initializer_list(expr->u.initializer_list);
    int declared_length = array_length_from_type(array_type);

    /* Check if this is a 2D array with nested initializer lists */
    int dims = count_array_dimensions(array_type);
    TypeSpecifier *element_type = array_element_type(array_type);
    if (!element_type)
    {
        fprintf(stderr, "array element type missing for initializer list\n");
        exit(1);
    }

    bool is_2d_array = dims == 2 && is_primitive_array(element_type);

    if (is_2d_array)
    {
        /* 2D array initialization: create array of arrays [[I
         * Each nested initializer list becomes an inner int[] array */
        TypeSpecifier *inner_type = element_type;
        TypeSpecifier *inner_elem_type = array_element_type(inner_type);
        int inner_len = array_length_from_type(inner_type);

        /* Find max column count from all row initializers.
         * Use the larger of type-declared size and max initializer size.
         * This handles incomplete initializers and cases where type info is wrong. */
        int max_init_cols = 0;
        if (expr->u.initializer_list)
        {
            for (ExpressionList *row = expr->u.initializer_list; row; row = row->next)
            {
                if (row->expression &&
                    row->expression->kind == INITIALIZER_LIST_EXPRESSION)
                {
                    int row_cols = count_initializer_list(row->expression->u.initializer_list);
                    if (row_cols > max_init_cols)
                    {
                        max_init_cols = row_cols;
                    }
                }
            }
        }
        if (max_init_cols > inner_len)
        {
            inner_len = max_init_cols;
        }

        /* Count rows (outer dimension) */
        int outer_len = declared_length ? declared_length : value_count;

        /* Create outer array: ANEWARRAY for array of int[] */
        codebuilder_build_iconst(cg->builder, (int32_t)outer_len);
        int array_class_idx = cg_find_or_add_array_class(cg, inner_type);
        codebuilder_build_anewarray(cg->builder, array_class_idx);

        /* Store outer array reference */
        int outer_local = allocate_temp_local_for_tag(cg, CF_VAL_OBJECT);
        codebuilder_build_astore(cg->builder, outer_local);

        /* Process each row (inner initializer list) */
        int row_idx = 0;
        for (ExpressionList *row = expr->u.initializer_list; row; row = row->next, row_idx++)
        {
            if (!row->expression)
                continue;

            /* Count elements in this row */
            int col_count = 0;
            if (row->expression->kind == INITIALIZER_LIST_EXPRESSION)
            {
                col_count = count_initializer_list(row->expression->u.initializer_list);
            }
            int row_len = inner_len ? inner_len : col_count;

            /* Pop row values from stack into temporaries */
            int *col_locals = NULL;
            if (col_count > 0)
            {
                col_locals = (int *)calloc(col_count, sizeof(int));
                for (int j = col_count; j > 0; --j)
                {
                    col_locals[j - 1] = allocate_temp_local(cg);
                    codebuilder_build_istore(cg->builder, col_locals[j - 1]);
                }
            }

            /* Create inner array for this row */
            codebuilder_build_iconst(cg->builder, (int32_t)row_len);
            if (!inner_elem_type)
            {
                fprintf(stderr, "array element type missing for 2D initializer\n");
                exit(1);
            }
            codebuilder_build_newarray(cg->builder, newarray_type_code(inner_elem_type));

            /* Store values into inner array */
            for (int j = 0; j < col_count; ++j)
            {
                codebuilder_build_dup(cg->builder);
                codebuilder_build_iconst(cg->builder, (int32_t)j);
                codebuilder_build_iload(cg->builder, col_locals[j]);
                codebuilder_build_iastore(cg->builder);
            }

            (void)col_locals;

            /* Store inner array into outer array: outer[row_idx] = inner */
            int inner_local = allocate_temp_local_for_tag(cg, CF_VAL_OBJECT);
            codebuilder_build_astore(cg->builder, inner_local);

            codebuilder_build_aload(cg->builder, outer_local);
            codebuilder_build_iconst(cg->builder, (int32_t)row_idx);
            codebuilder_build_aload(cg->builder, inner_local);
            codebuilder_build_aastore(cg->builder);
        }

        /* Load outer array reference as result */
        codebuilder_build_aload(cg->builder, outer_local);
    }
    else if (dims == 1 && element_type && cs_type_is_named(element_type) &&
             cs_type_is_basic_struct_or_union(element_type))
    {
        /* Struct array initialization: Foo arr[] = {{...}, {...}, ...}
         * Each nested initializer list creates a struct instance. */
        const char *struct_name = cs_type_user_type_name(element_type);
        int class_idx = find_class_index(cg, struct_name);
        if (class_idx == -1)
        {
            fprintf(stderr, "error: struct class not found: %s\n", struct_name);
            exit(1);
        }
        CG_ClassDef *class_def = &cg->class_defs[class_idx];

        /* Create outer array: ANEWARRAY for struct array */
        int length = declared_length ? declared_length : value_count;
        codebuilder_build_iconst(cg->builder, (int32_t)length);
        int const_idx = cg_find_or_add_class(cg, struct_name, class_idx);
        codebuilder_build_anewarray(cg->builder, const_idx);

        /* Store array reference in temp local */
        int array_local = allocate_temp_local_for_tag(cg, CF_VAL_OBJECT);
        codebuilder_build_astore(cg->builder, array_local);

        /* Process each element (nested initializer list) in reverse order
         * since values are on stack with last element on top */
        int elem_idx = value_count;
        for (ExpressionList *elem = expr->u.initializer_list; elem; elem = elem->next)
        {
            if (!elem->expression)
                continue;
            elem_idx--;

            /* Count fields in this initializer */
            int field_count = 0;
            int *field_indices = NULL;
            if (elem->expression->kind == INITIALIZER_LIST_EXPRESSION)
            {
                ExpressionList *init_list = elem->expression->u.initializer_list;
                for (ExpressionList *p = init_list; p; p = p->next)
                    field_count++;

                /* Check for designated initializers */
                if (init_list && init_list->expression &&
                    init_list->expression->kind == DESIGNATED_INITIALIZER_EXPRESSION)
                {
                    field_indices = (int *)calloc(field_count, sizeof(int));
                    int fi = 0;
                    for (ExpressionList *p = init_list; p; p = p->next, fi++)
                    {
                        Expression *di = p->expression;
                        if (di && di->kind == DESIGNATED_INITIALIZER_EXPRESSION)
                        {
                            const char *fname = di->u.designated_initializer.field_name;
                            for (int j = 0; j < class_def->field_count; j++)
                            {
                                if (class_def->fields[j].name && fname &&
                                    strcmp(class_def->fields[j].name, fname) == 0)
                                {
                                    field_indices[fi] = (int)j;
                                    break;
                                }
                            }
                        }
                        else
                        {
                            field_indices[fi] = -1;
                        }
                    }
                }
            }

            /* Extract type info for each field value for array-to-pointer conversion */
            TypeSpecifier **value_types = NULL;
            if (field_count > 0 && elem->expression->kind == INITIALIZER_LIST_EXPRESSION)
            {
                ExpressionList *init_list = elem->expression->u.initializer_list;
                value_types = (TypeSpecifier **)calloc(field_count, sizeof(TypeSpecifier *));
                int vi = 0;
                for (ExpressionList *p = init_list; p; p = p->next, vi++)
                {
                    if (p->expression)
                        value_types[vi] = p->expression->type;
                }
            }

            /* Create struct from values on stack (handles empty initializers too) */
            cg_emit_struct_from_init_values(cg, struct_name, field_indices, field_count, value_types);

            if (field_indices)
                free(field_indices);
            if (value_types)
                free(value_types);

            /* Store struct ref in temp local */
            int struct_local = allocate_temp_local_for_tag(cg, CF_VAL_OBJECT);
            codebuilder_build_astore(cg->builder, struct_local);

            /* Store struct into array: array[elem_idx] = struct */
            codebuilder_build_aload(cg->builder, array_local);
            codebuilder_build_iconst(cg->builder, (int32_t)elem_idx);
            codebuilder_build_aload(cg->builder, struct_local);
            codebuilder_build_aastore(cg->builder);
        }

        /* Load array reference as result */
        codebuilder_build_aload(cg->builder, array_local);
    }
    else if (value_count > 0)
    {
        /* 1D array initialization */
        /* Get element type tag for proper store/load instructions */
        CF_ValueTag element_tag = cg_array_element_value_tag(array_type);

        int *value_locals = (int *)calloc(value_count, sizeof(int));
        for (int i = 0; i < value_count; ++i)
        {
            value_locals[i] = allocate_temp_local_for_tag(cg, element_tag);
        }

        /* Store values from stack into temporaries using type-appropriate store */
        int idx = value_count;
        for (ExpressionList *p = expr->u.initializer_list; p; p = p->next)
        {
            if (idx == 0)
            {
                fprintf(stderr, "initializer list overflow\n");
                exit(1);
            }
            --idx;
            switch (element_tag)
            {
            case CF_VAL_INT:
                codebuilder_build_istore(cg->builder, value_locals[idx]);
                break;
            case CF_VAL_LONG:
                codebuilder_build_lstore(cg->builder, value_locals[idx]);
                break;
            case CF_VAL_FLOAT:
                codebuilder_build_fstore(cg->builder, value_locals[idx]);
                break;
            case CF_VAL_DOUBLE:
                codebuilder_build_dstore(cg->builder, value_locals[idx]);
                break;
            case CF_VAL_OBJECT:
            case CF_VAL_NULL:
                codebuilder_build_astore(cg->builder, value_locals[idx]);
                break;
            default:
                fprintf(stderr, "codegenvisitor_expr_complex.c: initlist store: invalid tag %d\n", element_tag);
                exit(1);
            }
        }

        int length = declared_length ? declared_length : value_count;
        codebuilder_build_iconst(cg->builder, (int32_t)length);

        /* Use anewarray for reference types (pointers, arrays, structs),
         * newarray for primitives */
        if (element_type && (cs_type_is_pointer(element_type) || cs_type_is_array(element_type)))
        {
            int array_class_idx = cg_find_or_add_array_class(cg, element_type);
            codebuilder_build_anewarray(cg->builder, array_class_idx);
        }
        else if (element_type && cs_type_is_named(element_type) &&
                 cs_type_is_basic_struct_or_union(element_type))
        {
            /* Struct array: use anewarray with struct class */
            const char *struct_name = cs_type_user_type_name(element_type);
            int class_idx = find_class_index(cg, struct_name);
            int const_idx = cg_find_or_add_class(cg, struct_name, class_idx);
            codebuilder_build_anewarray(cg->builder, const_idx);
        }
        else
        {
            codebuilder_build_newarray(cg->builder, newarray_type_code(element_type));
        }

        for (int i = 0; i < value_count; ++i)
        {
            codebuilder_build_dup(cg->builder);
            codebuilder_build_iconst(cg->builder, (int32_t)i);
            switch (element_tag)
            {
            case CF_VAL_INT:
                codebuilder_build_iload(cg->builder, value_locals[i]);
                break;
            case CF_VAL_LONG:
                codebuilder_build_lload(cg->builder, value_locals[i]);
                break;
            case CF_VAL_FLOAT:
                codebuilder_build_fload(cg->builder, value_locals[i]);
                break;
            case CF_VAL_DOUBLE:
                codebuilder_build_dload(cg->builder, value_locals[i]);
                break;
            case CF_VAL_OBJECT:
            case CF_VAL_NULL:
                codebuilder_build_aload(cg->builder, value_locals[i]);
                break;
            default:
                fprintf(stderr, "codegenvisitor_expr_complex.c: initlist load: invalid tag %d\n", element_tag);
                exit(1);
            }
            if (element_type && (cs_type_is_array(element_type) ||
                                 cs_type_is_pointer(element_type) ||
                                 (cs_type_is_named(element_type) &&
                                  cs_type_is_basic_struct_or_union(element_type))))
            {
                codebuilder_build_aastore(cg->builder);
            }
            else if (element_type && cs_type_is_double_exact(element_type))
            {
                codebuilder_build_dastore(cg->builder);
            }
            else if (element_type)
            {
                if (cs_type_is_char_exact(element_type) || cs_type_is_bool(element_type))
                {
                    codebuilder_build_bastore(cg->builder);
                }
                else if (cs_type_is_short_exact(element_type))
                {
                    codebuilder_build_sastore(cg->builder);
                }
                else if (cs_type_is_long_exact(element_type))
                {
                    codebuilder_build_lastore(cg->builder);
                }
                else if (cs_type_is_float_exact(element_type))
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
        }

        free(value_locals);
    }
    else
    {
        /* C23 empty initializer `= {}` for array - create zero-initialized array.
         * JVM arrays are already zero-initialized by newarray/anewarray. */
        int length = declared_length;
        if (length == 0)
        {
            /* VLA or unknown size - cannot use empty initializer */
            fprintf(stderr, "empty initializer for array requires known size\n");
            exit(1);
        }

        codebuilder_build_iconst(cg->builder, (int32_t)length);
        codebuilder_build_newarray(cg->builder, newarray_type_code(element_type));
    }

    handle_for_expression_leave(cg, expr);
}

void enter_funccallexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    mark_for_condition_start(cg, expr);

    Expression *func_expr = expr->u.function_call_expression.function;
    FunctionDeclaration *func = NULL;

    /* Skip validation for va_start/va_arg/va_end builtins */
    if (func_expr && func_expr->kind == IDENTIFIER_EXPRESSION)
    {
        const char *name = func_expr->u.identifier.name;
        if (name && (strcmp(name, "va_start") == 0 ||
                     strcmp(name, "__builtin_va_arg") == 0 ||
                     strcmp(name, "va_end") == 0))
        {
            return; /* Built-in, no function declaration needed */
        }

        func = func_expr->u.identifier.u.function;
        if (!func && name)
        {
            func = cs_search_function(cg->compiler, name);
            if (func)
            {
                func_expr->u.identifier.u.function = func;
                func_expr->u.identifier.is_function = true;
            }
        }
    }

    if (!func)
    {
        fprintf(stderr, "Error: function declaration missing for call");
        if (expr->line_number > 0)
        {
            fprintf(stderr, " at line %d", expr->line_number);
        }
        fprintf(stderr, "\n");
        exit(1);
    }
}

void leave_funccallexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    Expression *func_expr = expr->u.function_call_expression.function;
    ArgumentList *call_argument = expr->u.function_call_expression.argument;
    FunctionDeclaration *func = NULL;

    int actual = cs_count_arguments(call_argument);

    /* Handle va_start/va_arg/va_end as built-in operations */
    if (func_expr && func_expr->kind == IDENTIFIER_EXPRESSION)
    {
        const char *name = func_expr->u.identifier.name;

        if (name && strcmp(name, "va_start") == 0)
        {
            /* va_start(ap): ap = __objectPtr.create(__varargs, 0)
             * va_list is now void** (__objectPtr with Object[] base) */
            ArgumentList *args = call_argument;
            if (args && args->expr && args->expr->kind == IDENTIFIER_EXPRESSION)
            {
                Declaration *decl = args->expr->u.identifier.u.declaration;
                if (decl)
                {
                    codebuilder_build_pop(cg->builder); /* Pop ap value from stack */
                    CodegenSymbolInfo info = cg_ensure_symbol(cg, decl);

                    /* Load __varargs (Object[]) array */
                    int varargs_index = cg->current_function->varargs_index;
                    codebuilder_build_aload(cg->builder, varargs_index);

                    /* Push offset 0 */
                    codebuilder_build_iconst(cg->builder, 0);

                    /* Create __objectPtr inline */
                    cg_emit_ptr_create_by_type_index(cg, PTR_TYPE_OBJECT);

                    /* Store to ap */
                    codebuilder_build_astore(cg->builder, info.index);
                }
            }
            handle_for_expression_leave(cg, expr);
            return;
        }
        else if (name && strcmp(name, "__builtin_va_arg") == 0)
        {
            /* va_arg(ap): *ap++ using __objectPtr
             * ap is void** (__objectPtr with Object[] base)
             * 1. Get ap.base[ap.offset]
             * 2. Unbox based on return type
             * 3. Increment ap.offset
             */
            ArgumentList *args = call_argument;
            if (args && args->expr && args->expr->kind == IDENTIFIER_EXPRESSION)
            {
                Declaration *ap_decl = args->expr->u.identifier.u.declaration;
                if (ap_decl)
                {
                    CodegenSymbolInfo ap_info = cg_ensure_symbol(cg, ap_decl);
                    ConstantPoolBuilder *cp = code_output_cp(cg->output);

                    /* Pop the ap value from stack (sizeof doesn't generate code) */
                    codebuilder_build_pop(cg->builder);

                    /* Field refs for __objectPtr */
                    int base_field = cp_builder_add_fieldref(
                        cp, "__objectPtr", "base", "[Ljava/lang/Object;");
                    int offset_field = cp_builder_add_fieldref(
                        cp, "__objectPtr", "offset", "I");

                    /* 1. Get value: ap.base[ap.offset] */
                    codebuilder_build_aload(cg->builder, ap_info.index);
                    codebuilder_build_dup(cg->builder);
                    /* Stack: [ap, ap] */

                    codebuilder_build_getfield(cg->builder, base_field);
                    /* Stack: [ap, base] */

                    codebuilder_build_swap(cg->builder);
                    /* Stack: [base, ap] */

                    codebuilder_build_getfield(cg->builder, offset_field);
                    /* Stack: [base, offset] */

                    codebuilder_build_aaload(cg->builder);
                    /* Stack: [value (Object)] */

                    /* 2. Unbox based on return type */
                    TypeSpecifier *result_type = expr->type;
                    if (result_type)
                    {
                        if (cs_type_is_int_exact(result_type) ||
                            cs_type_is_char_exact(result_type) ||
                            cs_type_is_short_exact(result_type) ||
                            cs_type_is_bool(result_type))
                        {
                            /* checkcast Integer; invokevirtual intValue */
                            int class_idx = cp_builder_add_class(cp, "java/lang/Integer");
                            codebuilder_build_checkcast(cg->builder, class_idx);
                            int method_idx = cp_builder_add_methodref(
                                cp, "java/lang/Integer", "intValue", "()I");
                            codebuilder_build_invokevirtual(cg->builder, method_idx);
                        }
                        else if (cs_type_is_long_exact(result_type))
                        {
                            int class_idx = cp_builder_add_class(cp, "java/lang/Long");
                            codebuilder_build_checkcast(cg->builder, class_idx);
                            int method_idx = cp_builder_add_methodref(
                                cp, "java/lang/Long", "longValue", "()J");
                            codebuilder_build_invokevirtual(cg->builder, method_idx);
                        }
                        else if (cs_type_is_double_exact(result_type))
                        {
                            int class_idx = cp_builder_add_class(cp, "java/lang/Double");
                            codebuilder_build_checkcast(cg->builder, class_idx);
                            int method_idx = cp_builder_add_methodref(
                                cp, "java/lang/Double", "doubleValue", "()D");
                            codebuilder_build_invokevirtual(cg->builder, method_idx);
                        }
                        else if (cs_type_is_float_exact(result_type))
                        {
                            int class_idx = cp_builder_add_class(cp, "java/lang/Float");
                            codebuilder_build_checkcast(cg->builder, class_idx);
                            int method_idx = cp_builder_add_methodref(
                                cp, "java/lang/Float", "floatValue", "()F");
                            codebuilder_build_invokevirtual(cg->builder, method_idx);
                        }
                        else if (cs_type_is_pointer(result_type))
                        {
                            /* Check for void* - it's a direct Object reference */
                            TypeSpecifier *element = cs_type_child(result_type);
                            if (element && cs_type_is_void(element))
                            {
                                /* void* is just Object, no checkcast needed */
                            }
                            else
                            {
                                /* Other pointer types: checkcast to pointer wrapper class */
                                PtrTypeIndex ptr_index = (PtrTypeIndex)cg_pointer_runtime_kind(result_type);
                                int class_idx = cp_builder_add_class(cp, ptr_type_class_name(ptr_index));
                                codebuilder_build_checkcast(cg->builder, class_idx);
                            }
                        }
                        /* For other reference types, just leave as Object */
                    }

                    /* 3. Increment ap.offset: ap.offset = ap.offset + 1 */
                    codebuilder_build_aload(cg->builder, ap_info.index);
                    codebuilder_build_dup(cg->builder);
                    codebuilder_build_getfield(cg->builder, offset_field);
                    codebuilder_build_iconst(cg->builder, 1);
                    codebuilder_build_iadd(cg->builder);
                    codebuilder_build_putfield(cg->builder, offset_field);
                }
            }
            handle_for_expression_leave(cg, expr);
            return;
        }
        else if (name && strcmp(name, "va_end") == 0)
        {
            /* va_end(ap): no-op, just pop argument from stack */
            codebuilder_build_pop(cg->builder);
            handle_for_expression_leave(cg, expr);
            return;
        }
    }

    if (func_expr && func_expr->kind == IDENTIFIER_EXPRESSION)
    {
        func = func_expr->u.identifier.u.function;
        if (!func && func_expr->u.identifier.name)
        {
            func = cs_search_function(cg->compiler, func_expr->u.identifier.name);
            if (func)
            {
                func_expr->u.identifier.u.function = func;
                func_expr->u.identifier.is_function = true;
            }
        }
    }

    const char *target_class = NULL;
    if (func && func->class_name)
    {
        target_class = func->class_name;
    }
    else if (func && func->body && func->class_name)
    {
        target_class = func->class_name;
    }

    if (!func)
    {
        fprintf(stderr, "Error: function declaration missing for call");
        if (func_expr && func_expr->kind == IDENTIFIER_EXPRESSION && func_expr->u.identifier.name)
        {
            fprintf(stderr, " to '%s'", func_expr->u.identifier.name);
        }
        if (expr->line_number > 0)
        {
            fprintf(stderr, " at line %d", expr->line_number);
        }
        fprintf(stderr, "\n");
        exit(1);
    }

    int argc = cs_count_parameters(func->param);
    bool is_variadic = func->is_variadic;

    /* For variadic functions, actual args must be >= fixed params */
    if (is_variadic)
    {
        if (actual < argc)
        {
            fprintf(stderr, "Error: argument count mismatch for %s: expect at least %u got %u",
                    func->name ? func->name : "<anon>", argc, actual);
            if (expr->line_number > 0)
            {
                fprintf(stderr, " at line %d", expr->line_number);
            }
            fprintf(stderr, "\n");
            exit(1);
        }
    }
    else if (argc != actual)
    {
        fprintf(stderr, "Error: argument count mismatch for %s: expect %u got %u",
                func->name ? func->name : "<anon>", argc, actual);
        if (expr->line_number > 0)
        {
            fprintf(stderr, " at line %d", expr->line_number);
        }
        fprintf(stderr, "\n");
        exit(1);
    }

    /* Check for get_static attribute - emits GETSTATIC instead of call */
    AttributeSpecifier *get_static_attr = find_attribute(func->attributes,
                                                         CS_ATTRIBUTE_GET_STATIC);
    if (get_static_attr)
    {
        int pool_idx = cp_builder_add_fieldref(code_output_cp(cg->output),
                                               get_static_attr->class_name,
                                               get_static_attr->member_name,
                                               get_static_attr->descriptor);
        codebuilder_build_getstatic(cg->builder, pool_idx);
        handle_for_expression_leave(cg, expr);
        return;
    }

    /* Check for invoke_virtual attribute - emits INVOKEVIRTUAL instead of call */
    AttributeSpecifier *invoke_virtual_attr = find_attribute(func->attributes,
                                                             CS_ATTRIBUTE_INVOKE_VIRTUAL);
    if (invoke_virtual_attr)
    {
        /* For JVM verification, we need checkcast to convert receiver from Object
         * to the expected class type (e.g., String for String.length()).
         * If there are arguments on stack above the receiver, we must save them first. */
        const char *desc = invoke_virtual_attr->descriptor;
        int param_count = 0;
        char param_types[32]; /* Store type char for each param: I, J, D, F, L, [ */

        /* Count parameters from descriptor (e.g., "(I)C" has 1, "()I" has 0) */
        if (desc)
        {
            const char *p = strchr(desc, '(');
            if (p)
            {
                p++;
                while (*p && *p != ')' && param_count < 32)
                {
                    /* Record first char of type descriptor */
                    param_types[param_count] = *p;

                    /* Skip one type descriptor */
                    if (*p == 'L')
                    {
                        while (*p && *p != ';')
                            p++;
                        if (*p == ';')
                            p++;
                    }
                    else if (*p == '[')
                    {
                        p++;
                        if (*p == 'L')
                        {
                            while (*p && *p != ';')
                                p++;
                            if (*p == ';')
                                p++;
                        }
                        else if (*p)
                        {
                            p++;
                        }
                    }
                    else if (*p)
                    {
                        p++;
                    }
                    param_count++;
                }
            }
        }

        /* Get class index for checkcast */
        int class_idx = cg_find_or_add_class(cg, invoke_virtual_attr->class_name, -1);

        if (param_count > 0)
        {
            /* Begin temp scope for argument saving */
            codebuilder_begin_block(cg->builder);

            /* Save arguments to temp locals (in reverse order - top of stack first) */
            int *temp_locals = (int *)calloc(param_count, sizeof(int));
            for (int i = param_count - 1; i >= 0; i--)
            {
                /* Allocate local and use correct store based on type */
                switch (param_types[i])
                {
                case 'J': /* long - 2 slots */
                    temp_locals[i] = codebuilder_allocate_local(cg->builder, cb_type_long());
                    codebuilder_build_lstore(cg->builder, temp_locals[i]);
                    break;
                case 'D': /* double - 2 slots */
                    temp_locals[i] = codebuilder_allocate_local(cg->builder, cb_type_double());
                    codebuilder_build_dstore(cg->builder, temp_locals[i]);
                    break;
                case 'F': /* float */
                    temp_locals[i] = codebuilder_allocate_local(cg->builder, cb_type_float());
                    codebuilder_build_fstore(cg->builder, temp_locals[i]);
                    break;
                case 'L': /* object reference */
                case '[': /* array reference */
                    temp_locals[i] = codebuilder_allocate_local(cg->builder, cb_type_object("Ljava/lang/Object;"));
                    codebuilder_build_astore(cg->builder, temp_locals[i]);
                    break;
                default: /* I, B, C, S, Z - all use int on JVM stack */
                    temp_locals[i] = codebuilder_allocate_local(cg->builder, cb_type_int());
                    codebuilder_build_istore(cg->builder, temp_locals[i]);
                    break;
                }
            }

            /* Apply checkcast to receiver (now on top of stack) */
            codebuilder_build_checkcast(cg->builder, class_idx);

            /* Restore arguments from temp locals */
            for (int i = 0; i < param_count; i++)
            {
                switch (param_types[i])
                {
                case 'J':
                    codebuilder_build_lload(cg->builder, temp_locals[i]);
                    break;
                case 'D':
                    codebuilder_build_dload(cg->builder, temp_locals[i]);
                    break;
                case 'F':
                    codebuilder_build_fload(cg->builder, temp_locals[i]);
                    break;
                case 'L':
                case '[':
                    codebuilder_build_aload(cg->builder, temp_locals[i]);
                    break;
                default:
                    codebuilder_build_iload(cg->builder, temp_locals[i]);
                    break;
                }
            }
            free(temp_locals);

            /* End temp scope - slots can be reused */
            codebuilder_end_block(cg->builder);
        }
        else
        {
            /* No arguments - receiver is already on top, just checkcast */
            codebuilder_build_checkcast(cg->builder, class_idx);
        }

        int pool_idx = cp_builder_add_methodref(code_output_cp(cg->output),
                                                invoke_virtual_attr->class_name,
                                                invoke_virtual_attr->member_name,
                                                invoke_virtual_attr->descriptor);
        codebuilder_build_invokevirtual(cg->builder, pool_idx);
        handle_for_expression_leave(cg, expr);
        return;
    }

    /* Check for invoke_static attribute - emits INVOKESTATIC instead of call */
    AttributeSpecifier *invoke_static_attr = find_attribute(func->attributes,
                                                            CS_ATTRIBUTE_INVOKE_STATIC);
    if (invoke_static_attr)
    {
        /* Parse descriptor to get parameter types and add checkcast for object refs */
        const char *desc = invoke_static_attr->descriptor;
        int param_count = 0;
        char param_types[32];
        char *param_class_names[32];

        if (desc)
        {
            const char *p = strchr(desc, '(');
            if (p)
            {
                p++;
                while (*p && *p != ')' && param_count < 32)
                {
                    param_types[param_count] = *p;
                    param_class_names[param_count] = NULL;

                    if (*p == 'L')
                    {
                        const char *start = p + 1;
                        while (*p && *p != ';')
                            p++;
                        int len = (int)(p - start);
                        char *class_name = (char *)calloc(len + 1, sizeof(char));
                        for (int ci = 0; ci < len; ci++)
                            class_name[ci] = start[ci];
                        class_name[len] = '\0';
                        param_class_names[param_count] = class_name;
                        if (*p == ';')
                            p++;
                    }
                    else if (*p == '[')
                    {
                        p++;
                        if (*p == 'L')
                        {
                            while (*p && *p != ';')
                                p++;
                            if (*p == ';')
                                p++;
                        }
                        else if (*p)
                        {
                            p++;
                        }
                    }
                    else if (*p)
                    {
                        p++;
                    }
                    param_count++;
                }
            }
        }

        /* Check if any parameter needs checkcast */
        int needs_checkcast = 0;
        for (int ci = 0; ci < param_count; ci++)
        {
            if (param_types[ci] == 'L' && param_class_names[ci] != NULL)
            {
                needs_checkcast = 1;
                break;
            }
        }

        if (needs_checkcast && param_count > 0)
        {
            codebuilder_begin_block(cg->builder);

            int *temp_locals = (int *)calloc(param_count, sizeof(int));

            /* Pop args in reverse order */
            for (int ci = param_count - 1; ci >= 0; ci--)
            {
                switch (param_types[ci])
                {
                case 'J':
                    temp_locals[ci] = codebuilder_allocate_local(cg->builder, cb_type_long());
                    codebuilder_build_lstore(cg->builder, temp_locals[ci]);
                    break;
                case 'D':
                    temp_locals[ci] = codebuilder_allocate_local(cg->builder, cb_type_double());
                    codebuilder_build_dstore(cg->builder, temp_locals[ci]);
                    break;
                case 'F':
                    temp_locals[ci] = codebuilder_allocate_local(cg->builder, cb_type_float());
                    codebuilder_build_fstore(cg->builder, temp_locals[ci]);
                    break;
                case 'L':
                case '[':
                    temp_locals[ci] = codebuilder_allocate_local(cg->builder, cb_type_object("Ljava/lang/Object;"));
                    codebuilder_build_astore(cg->builder, temp_locals[ci]);
                    break;
                default:
                    temp_locals[ci] = codebuilder_allocate_local(cg->builder, cb_type_int());
                    codebuilder_build_istore(cg->builder, temp_locals[ci]);
                    break;
                }
            }

            /* Restore args with checkcast for object types */
            for (int ci = 0; ci < param_count; ci++)
            {
                switch (param_types[ci])
                {
                case 'J':
                    codebuilder_build_lload(cg->builder, temp_locals[ci]);
                    break;
                case 'D':
                    codebuilder_build_dload(cg->builder, temp_locals[ci]);
                    break;
                case 'F':
                    codebuilder_build_fload(cg->builder, temp_locals[ci]);
                    break;
                case 'L':
                    codebuilder_build_aload(cg->builder, temp_locals[ci]);
                    if (param_class_names[ci] != NULL)
                    {
                        int class_idx = cg_find_or_add_class(cg, param_class_names[ci], -1);
                        codebuilder_build_checkcast(cg->builder, class_idx);
                    }
                    break;
                case '[':
                    codebuilder_build_aload(cg->builder, temp_locals[ci]);
                    break;
                default:
                    codebuilder_build_iload(cg->builder, temp_locals[ci]);
                    break;
                }
            }
            free(temp_locals);
            codebuilder_end_block(cg->builder);
        }

        /* Free class name strings */
        for (int ci = 0; ci < param_count; ci++)
        {
            if (param_class_names[ci] != NULL)
                free(param_class_names[ci]);
        }

        int pool_idx = cp_builder_add_methodref(code_output_cp(cg->output),
                                                invoke_static_attr->class_name,
                                                invoke_static_attr->member_name,
                                                invoke_static_attr->descriptor);
        codebuilder_build_invokestatic(cg->builder, pool_idx);
        handle_for_expression_leave(cg, expr);
        return;
    }

    /* Check for invoke_special attribute - emits INVOKESPECIAL instead of call */
    AttributeSpecifier *invoke_special_attr = find_attribute(func->attributes,
                                                             CS_ATTRIBUTE_INVOKE_SPECIAL);
    if (invoke_special_attr)
    {
        /* Parse descriptor to get parameter types and add checkcast for object refs */
        const char *desc = invoke_special_attr->descriptor;
        int param_count = 0;
        char param_types[32];
        char *param_class_names[32]; /* Class names for L types */

        if (desc)
        {
            const char *p = strchr(desc, '(');
            if (p)
            {
                p++;
                while (*p && *p != ')' && param_count < 32)
                {
                    param_types[param_count] = *p;
                    param_class_names[param_count] = NULL;

                    if (*p == 'L')
                    {
                        /* Extract class name for checkcast */
                        const char *start = p + 1;
                        while (*p && *p != ';')
                            p++;
                        int len = (int)(p - start);
                        char *class_name = (char *)calloc(len + 1, sizeof(char));
                        for (int i = 0; i < len; i++)
                            class_name[i] = start[i];
                        class_name[len] = '\0';
                        param_class_names[param_count] = class_name;
                        if (*p == ';')
                            p++;
                    }
                    else if (*p == '[')
                    {
                        p++;
                        if (*p == 'L')
                        {
                            while (*p && *p != ';')
                                p++;
                            if (*p == ';')
                                p++;
                        }
                        else if (*p)
                        {
                            p++;
                        }
                    }
                    else if (*p)
                    {
                        p++;
                    }
                    param_count++;
                }
            }
        }

        /* Check if any parameter needs checkcast */
        int needs_checkcast = 0;
        for (int i = 0; i < param_count; i++)
        {
            if (param_types[i] == 'L' && param_class_names[i] != NULL)
            {
                needs_checkcast = 1;
                break;
            }
        }

        if (needs_checkcast && param_count > 0)
        {
            /* Begin temp scope for argument saving */
            codebuilder_begin_block(cg->builder);

            /* Save all args to temp locals, apply checkcast where needed, restore */
            int *temp_locals = (int *)calloc(param_count, sizeof(int));

            /* Pop args in reverse order */
            for (int i = param_count - 1; i >= 0; i--)
            {
                switch (param_types[i])
                {
                case 'J':
                    temp_locals[i] = codebuilder_allocate_local(cg->builder, cb_type_long());
                    codebuilder_build_lstore(cg->builder, temp_locals[i]);
                    break;
                case 'D':
                    temp_locals[i] = codebuilder_allocate_local(cg->builder, cb_type_double());
                    codebuilder_build_dstore(cg->builder, temp_locals[i]);
                    break;
                case 'F':
                    temp_locals[i] = codebuilder_allocate_local(cg->builder, cb_type_float());
                    codebuilder_build_fstore(cg->builder, temp_locals[i]);
                    break;
                case 'L':
                case '[':
                    temp_locals[i] = codebuilder_allocate_local(cg->builder, cb_type_object("Ljava/lang/Object;"));
                    codebuilder_build_astore(cg->builder, temp_locals[i]);
                    break;
                default:
                    temp_locals[i] = codebuilder_allocate_local(cg->builder, cb_type_int());
                    codebuilder_build_istore(cg->builder, temp_locals[i]);
                    break;
                }
            }

            /* Restore args with checkcast for object types */
            for (int i = 0; i < param_count; i++)
            {
                switch (param_types[i])
                {
                case 'J':
                    codebuilder_build_lload(cg->builder, temp_locals[i]);
                    break;
                case 'D':
                    codebuilder_build_dload(cg->builder, temp_locals[i]);
                    break;
                case 'F':
                    codebuilder_build_fload(cg->builder, temp_locals[i]);
                    break;
                case 'L':
                    codebuilder_build_aload(cg->builder, temp_locals[i]);
                    if (param_class_names[i] != NULL)
                    {
                        int class_idx = cg_find_or_add_class(cg, param_class_names[i], -1);
                        codebuilder_build_checkcast(cg->builder, class_idx);
                    }
                    break;
                case '[':
                    codebuilder_build_aload(cg->builder, temp_locals[i]);
                    break;
                default:
                    codebuilder_build_iload(cg->builder, temp_locals[i]);
                    break;
                }
            }
            free(temp_locals);

            /* End temp scope - slots can be reused */
            codebuilder_end_block(cg->builder);
        }

        /* Free class name strings */
        for (int i = 0; i < param_count; i++)
        {
            if (param_class_names[i] != NULL)
                free(param_class_names[i]);
        }

        int pool_idx = cp_builder_add_methodref(code_output_cp(cg->output),
                                                invoke_special_attr->class_name,
                                                invoke_special_attr->member_name,
                                                invoke_special_attr->descriptor);
        codebuilder_build_invokespecial(cg->builder, pool_idx);
        handle_for_expression_leave(cg, expr);
        return;
    }

    /* Check for new attribute - emits NEW + DUP for object allocation */
    AttributeSpecifier *new_attr = find_attribute(func->attributes,
                                                  CS_ATTRIBUTE_NEW);
    if (new_attr)
    {
        int class_idx = cg_find_or_add_class(cg, new_attr->class_name, -1);
        codebuilder_build_new(cg->builder, class_idx);
        codebuilder_build_dup(cg->builder);
        handle_for_expression_leave(cg, expr);
        return;
    }

    /* Check for get_field attribute - emits GETFIELD instruction */
    AttributeSpecifier *get_field_attr = find_attribute(func->attributes,
                                                        CS_ATTRIBUTE_GET_FIELD);
    if (get_field_attr)
    {
        int pool_idx = cp_builder_add_fieldref(code_output_cp(cg->output),
                                               get_field_attr->class_name,
                                               get_field_attr->member_name,
                                               get_field_attr->descriptor);
        codebuilder_build_getfield(cg->builder, pool_idx);
        handle_for_expression_leave(cg, expr);
        return;
    }

    /* Check for arraylength attribute - emits ARRAYLENGTH instruction */
    AttributeSpecifier *arraylength_attr = find_attribute(func->attributes,
                                                          CS_ATTRIBUTE_ARRAYLENGTH);
    if (arraylength_attr)
    {
        codebuilder_build_arraylength(cg->builder);
        handle_for_expression_leave(cg, expr);
        return;
    }

    /* Check for aaload attribute - emits AALOAD instruction */
    AttributeSpecifier *aaload_attr = find_attribute(func->attributes,
                                                     CS_ATTRIBUTE_AALOAD);
    if (aaload_attr)
    {
        codebuilder_build_aaload(cg->builder);
        handle_for_expression_leave(cg, expr);
        return;
    }

    /* Check for forbidden functions: malloc, realloc */
    if (func->name && strcmp(func->name, "malloc") == 0)
    {
        fprintf(stderr, "Error: malloc is not supported in Cminor, use calloc instead");
        if (expr->line_number > 0)
        {
            fprintf(stderr, " at line %d", expr->line_number);
        }
        fprintf(stderr, "\n");
        exit(1);
    }
    if (func->name && strcmp(func->name, "realloc") == 0)
    {
        fprintf(stderr, "Error: realloc is not supported in Cminor");
        if (expr->line_number > 0)
        {
            fprintf(stderr, " at line %d", expr->line_number);
        }
        fprintf(stderr, "\n");
        exit(1);
    }

    /* Check for calloc with sizeof(Struct) - generates struct array with initialization */
    if (func->name && strcmp(func->name, "calloc") == 0 && actual == 2)
    {
        /* Get second argument (sizeof expression) */
        ArgumentList *arg1 = call_argument;
        ArgumentList *arg2 = arg1 ? arg1->next : NULL;
        Expression *sizeof_expr = arg2 ? arg2->expr : NULL;

        if (sizeof_expr && sizeof_expr->kind == SIZEOF_EXPRESSION &&
            sizeof_expr->u.sizeof_expression.is_type)
        {
            TypeSpecifier *sizeof_type = sizeof_expr->u.sizeof_expression.type;

            /* Check if it's a struct type.
             * Skip typedef aliases for primitive types (e.g., uint32_t). */
            if (sizeof_type && cs_type_is_named(sizeof_type) && cs_type_is_basic_struct_or_union(sizeof_type))
            {
                const char *struct_name = cs_type_user_type_name(sizeof_type);
                if (struct_name)
                {
                    /* Stack has: [n] (first argument, count)
                     * sizeof is not on stack (it's noop)
                     *
                     * Generate:
                     *   istore temp_n
                     *   iload temp_n
                     *   anewarray StructName
                     *   astore temp_arr
                     *   iconst 0
                     *   istore temp_i
                     * loop:
                     *   iload temp_i
                     *   iload temp_n
                     *   if_icmpge end
                     *   aload temp_arr
                     *   iload temp_i
                     *   new StructName
                     *   dup
                     *   invokespecial StructName.<init>()V
                     *   aastore
                     *   iinc temp_i 1
                     *   goto loop
                     * end:
                     *   new __objectPtr
                     *   dup
                     *   invokespecial __objectPtr.<init>()V
                     *   dup
                     *   aload temp_arr
                     *   putfield __objectPtr.base
                     *   dup
                     *   iconst 0
                     *   putfield __objectPtr.offset
                     */

                    /* Allocate temp locals */
                    int temp_n = allocate_temp_local(cg);
                    int temp_arr = allocate_temp_local_for_tag(cg, CF_VAL_OBJECT);
                    int temp_i = allocate_temp_local(cg);

                    /* Store n from stack */
                    codebuilder_build_istore(cg->builder, temp_n);

                    /* Create array: new StructName[n] */
                    codebuilder_build_iload(cg->builder, temp_n);
                    int struct_class_idx = cg_find_or_add_class(cg, struct_name, -1);
                    codebuilder_build_anewarray(cg->builder, struct_class_idx);
                    codebuilder_build_astore(cg->builder, temp_arr);

                    /* Initialize loop counter */
                    codebuilder_build_iconst(cg->builder, 0);
                    codebuilder_build_istore(cg->builder, temp_i);

                    /* Loop to initialize each element */
                    CB_Label *loop_start = codebuilder_create_label(cg->builder);
                    CB_Label *loop_end = codebuilder_create_label(cg->builder);

                    codebuilder_mark_loop_header(cg->builder, loop_start);
                    codebuilder_place_label(cg->builder, loop_start);

                    /* if (i >= n) goto end */
                    codebuilder_build_iload(cg->builder, temp_i);
                    codebuilder_build_iload(cg->builder, temp_n);
                    codebuilder_jump_if_icmp(cg->builder, ICMP_GE, loop_end);

                    /* arr[i] = new StructName() with embedded struct initialization */
                    codebuilder_build_aload(cg->builder, temp_arr);
                    codebuilder_build_iload(cg->builder, temp_i);

                    /* Use cg_emit_struct_from_init_values for recursive embedded struct init */
                    cg_emit_struct_from_init_values(cg, struct_name, NULL, 0, NULL);
                    /* Stack: [arr, i, struct_ref] */

                    codebuilder_build_aastore(cg->builder);

                    /* i++ */
                    codebuilder_build_iinc(cg->builder, temp_i, 1);
                    codebuilder_jump(cg->builder, loop_start);

                    codebuilder_place_label(cg->builder, loop_end);

                    /* Create __objectPtr and wrap the array */
                    ptr_usage_mark(PTR_TYPE_OBJECT);

                    const char *ptr_class = "__objectPtr";
                    int ptr_class_idx = cg_find_or_add_class(cg, ptr_class, -1);

                    codebuilder_build_new(cg->builder, ptr_class_idx);
                    codebuilder_build_dup(cg->builder);
                    int ptr_init_idx = cp_builder_add_methodref(code_output_cp(cg->output),
                                                                ptr_class, "<init>", "()V");
                    codebuilder_build_invokespecial(cg->builder, ptr_init_idx);

                    /* ptr.base = arr */
                    codebuilder_build_dup(cg->builder);
                    codebuilder_build_aload(cg->builder, temp_arr);
                    int base_field_idx = cp_builder_add_fieldref(code_output_cp(cg->output),
                                                                 ptr_class, "base",
                                                                 "[Ljava/lang/Object;");
                    codebuilder_build_putfield(cg->builder, base_field_idx);

                    /* ptr.offset = 0 */
                    codebuilder_build_dup(cg->builder);
                    codebuilder_build_iconst(cg->builder, 0);
                    int offset_field_idx = cp_builder_add_fieldref(code_output_cp(cg->output),
                                                                   ptr_class, "offset", "I");
                    codebuilder_build_putfield(cg->builder, offset_field_idx);

                    /* Result: __objectPtr on stack */
                    handle_for_expression_leave(cg, expr);
                    return;
                }
            }
            else if (sizeof_type && cs_type_is_pointer(sizeof_type))
            {
                /* Pointer type: calloc(n, sizeof(Foo *))
                 * Stack has: [n] (first argument, count)
                 * sizeof is not on stack (it's noop)
                 * Result type is T** (pointer to pointer), so use __objectPtr
                 *
                 * Each array element must be initialized with a null pointer wrapper
                 * (not Java null) to satisfy Cminor's pointer representation.
                 */
                codebuilder_begin_block(cg->builder);

                /* Allocate temp locals */
                int temp_n = codebuilder_allocate_local(cg->builder, cb_type_int());
                int temp_arr = codebuilder_allocate_local(cg->builder, cb_type_object("Ljava/lang/Object;"));
                int temp_i = codebuilder_allocate_local(cg->builder, cb_type_int());

                /* Store n from stack */
                codebuilder_build_istore(cg->builder, temp_n);

                /* Create array: new Object[n] */
                codebuilder_build_iload(cg->builder, temp_n);
                int obj_class = cg_find_or_add_object_class(cg);
                codebuilder_build_anewarray(cg->builder, obj_class);
                codebuilder_build_astore(cg->builder, temp_arr);

                /* Initialize loop counter */
                codebuilder_build_iconst(cg->builder, 0);
                codebuilder_build_istore(cg->builder, temp_i);

                /* Loop to initialize each element with null pointer wrapper */
                CB_Label *loop_start = codebuilder_create_label(cg->builder);
                CB_Label *loop_end = codebuilder_create_label(cg->builder);

                codebuilder_mark_loop_header(cg->builder, loop_start);
                codebuilder_place_label(cg->builder, loop_start);

                /* if (i >= n) goto end */
                codebuilder_build_iload(cg->builder, temp_i);
                codebuilder_build_iload(cg->builder, temp_n);
                codebuilder_jump_if_icmp(cg->builder, ICMP_GE, loop_end);

                /* arr[i] = new pointer_wrapper(base=null, offset=0) */
                codebuilder_build_aload(cg->builder, temp_arr);
                codebuilder_build_iload(cg->builder, temp_i);

                /* Create null pointer wrapper: base=null, offset=0 */
                codebuilder_build_aconst_null(cg->builder);
                codebuilder_build_iconst(cg->builder, 0);
                cg_emit_ptr_create(cg, sizeof_type);
                /* Stack: [arr, i, ptr_wrapper] */

                codebuilder_build_aastore(cg->builder);

                /* i++ */
                codebuilder_build_iinc(cg->builder, temp_i, 1);
                codebuilder_jump(cg->builder, loop_start);

                codebuilder_place_label(cg->builder, loop_end);

                /* Create outer __objectPtr and wrap the array */
                codebuilder_build_aload(cg->builder, temp_arr);
                codebuilder_build_iconst(cg->builder, 0);
                /* Always use PTR_TYPE_OBJECT for pointer arrays (T**) */
                cg_emit_ptr_create_by_type_index(cg, PTR_TYPE_OBJECT);

                codebuilder_end_block(cg->builder);

                handle_for_expression_leave(cg, expr);
                return;
            }
            else if (sizeof_type)
            {
                /* Primitive type: calloc(n, sizeof(int/double/char/etc.))
                 * Stack has: [n] (first argument, count)
                 * sizeof is not on stack (it's noop)
                 *
                 * Generate:
                 *   newarray T_TYPE
                 *   iconst 0
                 *   (inline ptr_create)
                 */
                codebuilder_build_newarray(cg->builder, newarray_type_code(sizeof_type));
                codebuilder_build_iconst(cg->builder, 0);
                cg_emit_ptr_create(cg, sizeof_type);

                handle_for_expression_leave(cg, expr);
                return;
            }
        }
    }

    if (!target_class)
    {
        target_class = cg->current_class_name;
    }

    /* Generate deep copies for struct-type arguments (C value semantics).
     * Stack has all arguments pushed. We need to save them, generate copies
     * for struct types, and push them back. */
    bool has_struct_args = false;
    ArgumentList *arg_check;
    for (arg_check = call_argument; arg_check; arg_check = arg_check->next)
    {
        if (arg_check->expr && arg_check->expr->type &&
            cs_type_is_named(arg_check->expr->type) && cs_type_is_basic_struct_or_union(arg_check->expr->type))
        {
            has_struct_args = true;
            break;
        }
    }

    if (has_struct_args && actual > 0)
    {
        /* Begin temp scope for argument saving */
        codebuilder_begin_block(cg->builder);

        int *arg_locals = (int *)calloc(actual, sizeof(int));
        TypeSpecifier **arg_types = (TypeSpecifier **)calloc(actual, sizeof(TypeSpecifier *));

        /* Build array of argument types */
        int idx = 0;
        for (ArgumentList *a = call_argument; a; a = a->next, idx++)
        {
            arg_types[idx] = a->expr ? a->expr->type : NULL;
        }

        /* Pop arguments into temp locals (last argument is on top of stack) */
        for (int j = actual - 1; j >= 0; j--)
        {
            TypeSpecifier *t = arg_types[j];
            if (t && cs_type_is_long_exact(t))
            {
                arg_locals[j] = codebuilder_allocate_local(cg->builder, cb_type_long());
                codebuilder_build_lstore(cg->builder, arg_locals[j]);
            }
            else if (t && cs_type_is_double_exact(t))
            {
                arg_locals[j] = codebuilder_allocate_local(cg->builder, cb_type_double());
                codebuilder_build_dstore(cg->builder, arg_locals[j]);
            }
            else if (t && cs_type_is_float_exact(t))
            {
                arg_locals[j] = codebuilder_allocate_local(cg->builder, cb_type_float());
                codebuilder_build_fstore(cg->builder, arg_locals[j]);
            }
            else if (t && (cs_type_is_basic_struct_or_union(t) || cs_type_is_pointer(t) || cs_type_is_array(t)))
            {
                arg_locals[j] = codebuilder_allocate_local(cg->builder, cb_type_object("Ljava/lang/Object;"));
                codebuilder_build_astore(cg->builder, arg_locals[j]);
            }
            else
            {
                arg_locals[j] = codebuilder_allocate_local(cg->builder, cb_type_int());
                codebuilder_build_istore(cg->builder, arg_locals[j]);
            }
        }

        /* Push arguments back, generating deep copies for struct types */
        for (int j = 0; j < actual; j++)
        {
            TypeSpecifier *t = arg_types[j];
            if (t && cs_type_is_long_exact(t))
            {
                codebuilder_build_lload(cg->builder, arg_locals[j]);
            }
            else if (t && cs_type_is_double_exact(t))
            {
                codebuilder_build_dload(cg->builder, arg_locals[j]);
            }
            else if (t && cs_type_is_float_exact(t))
            {
                codebuilder_build_fload(cg->builder, arg_locals[j]);
            }
            else if (t && (cs_type_is_basic_struct_or_union(t) || cs_type_is_pointer(t) || cs_type_is_array(t)))
            {
                codebuilder_build_aload(cg->builder, arg_locals[j]);
                /* Generate deep copy for struct types.
                 * Skip typedef aliases for primitive types (e.g., uint32_t). */
                if (cs_type_is_named(t) && cs_type_is_basic_struct_or_union(t))
                {
                    cg_emit_struct_deep_copy(cg, t);
                }
            }
            else
            {
                codebuilder_build_iload(cg->builder, arg_locals[j]);
            }
        }

        (void)arg_locals;
        (void)arg_types;

        /* End temp scope - slots can be reused */
        codebuilder_end_block(cg->builder);
    }

    /* Pack variadic arguments into Object[] for variadic function calls */
    if (is_variadic)
    {
        /* Begin temp scope for argument saving */
        codebuilder_begin_block(cg->builder);

        int vararg_count = actual - argc;
        if (vararg_count < 0)
            vararg_count = 0;

        /* Save all arguments to temp locals (stack has all args) */
        int *temp_locals = (int *)calloc(actual, sizeof(int));
        TypeSpecifier **arg_types_va = (TypeSpecifier **)calloc(actual, sizeof(TypeSpecifier *));

        int idx = 0;
        for (ArgumentList *a = call_argument; a; a = a->next, idx++)
        {
            arg_types_va[idx] = a->expr ? a->expr->type : NULL;
        }

        /* Pop all args into temp locals (last arg on top) */
        for (int j = actual - 1; j >= 0; j--)
        {
            TypeSpecifier *t = arg_types_va[j];
            if (t && cs_type_is_long_exact(t))
            {
                temp_locals[j] = codebuilder_allocate_local(cg->builder, cb_type_long());
                codebuilder_build_lstore(cg->builder, temp_locals[j]);
            }
            else if (t && cs_type_is_double_exact(t))
            {
                temp_locals[j] = codebuilder_allocate_local(cg->builder, cb_type_double());
                codebuilder_build_dstore(cg->builder, temp_locals[j]);
            }
            else if (t && cs_type_is_float_exact(t))
            {
                temp_locals[j] = codebuilder_allocate_local(cg->builder, cb_type_float());
                codebuilder_build_fstore(cg->builder, temp_locals[j]);
            }
            else if (t && (cs_type_is_pointer(t) || cs_type_is_array(t) || cs_type_is_basic_struct_or_union(t)))
            {
                temp_locals[j] = codebuilder_allocate_local(cg->builder, cb_type_object("Ljava/lang/Object;"));
                codebuilder_build_astore(cg->builder, temp_locals[j]);
            }
            else
            {
                temp_locals[j] = codebuilder_allocate_local(cg->builder, cb_type_int());
                codebuilder_build_istore(cg->builder, temp_locals[j]);
            }
        }

        /* Push fixed args back */
        for (int j = 0; j < argc; j++)
        {
            TypeSpecifier *t = arg_types_va[j];
            if (t && cs_type_is_long_exact(t))
            {
                codebuilder_build_lload(cg->builder, temp_locals[j]);
            }
            else if (t && cs_type_is_double_exact(t))
            {
                codebuilder_build_dload(cg->builder, temp_locals[j]);
            }
            else if (t && cs_type_is_float_exact(t))
            {
                codebuilder_build_fload(cg->builder, temp_locals[j]);
            }
            else if (t && (cs_type_is_pointer(t) || cs_type_is_array(t) || cs_type_is_basic_struct_or_union(t)))
            {
                codebuilder_build_aload(cg->builder, temp_locals[j]);
            }
            else
            {
                codebuilder_build_iload(cg->builder, temp_locals[j]);
            }
        }

        /* Create Object[] for varargs and box each value */
        codebuilder_build_iconst(cg->builder, vararg_count);
        int object_class = cp_builder_add_class(code_output_cp(cg->output), "java/lang/Object");
        codebuilder_build_anewarray(cg->builder, object_class);

        for (int j = 0; j < vararg_count; j++)
        {
            int src_idx = argc + j;
            TypeSpecifier *t = arg_types_va[src_idx];

            codebuilder_build_dup(cg->builder);
            codebuilder_build_iconst(cg->builder, j);

            /* Box the value */
            if (t && cs_type_is_int_exact(t))
            {
                codebuilder_build_iload(cg->builder, temp_locals[src_idx]);
                int method_idx = cp_builder_add_methodref(code_output_cp(cg->output),
                                                          "java/lang/Integer", "valueOf", "(I)Ljava/lang/Integer;");
                codebuilder_build_invokestatic(cg->builder, method_idx);
            }
            else if (t && cs_type_is_long_exact(t))
            {
                codebuilder_build_lload(cg->builder, temp_locals[src_idx]);
                int method_idx = cp_builder_add_methodref(code_output_cp(cg->output),
                                                          "java/lang/Long", "valueOf", "(J)Ljava/lang/Long;");
                codebuilder_build_invokestatic(cg->builder, method_idx);
            }
            else if (t && cs_type_is_double_exact(t))
            {
                codebuilder_build_dload(cg->builder, temp_locals[src_idx]);
                int method_idx = cp_builder_add_methodref(code_output_cp(cg->output),
                                                          "java/lang/Double", "valueOf", "(D)Ljava/lang/Double;");
                codebuilder_build_invokestatic(cg->builder, method_idx);
            }
            else if (t && cs_type_is_float_exact(t))
            {
                codebuilder_build_fload(cg->builder, temp_locals[src_idx]);
                int method_idx = cp_builder_add_methodref(code_output_cp(cg->output),
                                                          "java/lang/Float", "valueOf", "(F)Ljava/lang/Float;");
                codebuilder_build_invokestatic(cg->builder, method_idx);
            }
            else if (t && (cs_type_is_pointer(t) || cs_type_is_array(t) || cs_type_is_basic_struct_or_union(t)))
            {
                codebuilder_build_aload(cg->builder, temp_locals[src_idx]);
            }
            else
            {
                /* Default: treat as int */
                codebuilder_build_iload(cg->builder, temp_locals[src_idx]);
                int method_idx = cp_builder_add_methodref(code_output_cp(cg->output),
                                                          "java/lang/Integer", "valueOf", "(I)Ljava/lang/Integer;");
                codebuilder_build_invokestatic(cg->builder, method_idx);
            }

            codebuilder_build_aastore(cg->builder);
        }

        free(temp_locals);
        free(arg_types_va);

        /* End temp scope - slots can be reused */
        codebuilder_end_block(cg->builder);
    }

    bool same_class = target_class && cg->current_class_name &&
                      strcmp(target_class, cg->current_class_name) == 0 && func->body;

    if (!same_class)
    {
        int argc = cs_count_parameters(func->param);
        if (func->is_variadic)
        {
            argc += 1;
        }
        int pool_idx = cp_builder_add_methodref_typed(code_output_cp(cg->output), target_class,
                                                      resolve_function_name(func),
                                                      cg_function_descriptor(func),
                                                      func, argc);
        codebuilder_build_invokestatic(cg->builder, pool_idx);
        handle_for_expression_leave(cg, expr);
        return;
    }

    int pool_idx = (func->index >= 0) ? func->index
                                      : cg_add_method(cg, func);
    codebuilder_build_invokestatic(cg->builder, pool_idx);
    handle_for_expression_leave(cg, expr);
}
