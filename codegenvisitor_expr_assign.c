#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codegen_constants.h"
#include "codegen_jvm_types.h"
#include "codegen_symbols.h"
#include "codegenvisitor.h"
#include "codegenvisitor_expr_assign.h"
#include "codebuilder_ptr.h"
#include "codegenvisitor_util.h"
#include "codegenvisitor_expr_util.h"
#include "codebuilder_part1.h"
#include "codebuilder_part2.h"
#include "codebuilder_part3.h"
#include "cminor_type.h"
#include "synthetic_codegen.h"

void enter_assignexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    mark_for_condition_start(cg, expr);

    Expression *left = expr->u.assignment_expression.left;
    cg->ctx.assign_is_simple = (expr->u.assignment_expression.aope == ASSIGN);
    cg->ctx.assign_target = left;
    if (left && (left->kind == ARRAY_EXPRESSION || left->kind == MEMBER_EXPRESSION ||
                 left->kind == DEREFERENCE_EXPRESSION))
    {
        cg->ctx.assign_target = left;
    }

    /* For heap-lifted identifier assignment, mark it so we load the array ref
     * instead of the element value */
    if (left && left->kind == IDENTIFIER_EXPRESSION && !left->u.identifier.is_function)
    {
        Declaration *decl = left->u.identifier.u.declaration;
        if (decl && decl->needs_heap_lift)
        {
            cg->ctx.assign_target = left;
        }
    }
}

void leave_assignexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    AssignmentOperator aope = expr->u.assignment_expression.aope;
    Expression *left = expr->u.assignment_expression.left;
    Expression *right = expr->u.assignment_expression.right;
    /* Determine simple_assign directly from operator to handle chained assignments correctly */
    bool simple_assign = (aope == ASSIGN);
    cg->ctx.assign_is_simple = false;

    if (!left)
    {
        fprintf(stderr, "assignment target missing\n");
        exit(1);
    }

    if (left->kind == ARRAY_EXPRESSION)
    {
        cg->ctx.assign_target = NULL;

        Expression *array_base = left->u.array_expression.array;
        TypeSpecifier *array_type = array_base ? array_base->type : NULL;
        TypeSpecifier *element_type = array_type ? cs_type_child(array_type) : NULL;

        /* Pointer subscript assignment: ptr[i] = value
         * Stack before: [..., ptr, index, value]
         * Use inline ptr_store_subscript
         */
        if (array_type && cs_type_is_pointer(array_type))
        {
            TypeSpecifier *pointee = cs_type_child(array_type);
            /* For struct pointer, use __objectPtr handling.
             * Skip typedef aliases for primitive types (e.g., uint32_t*). */
            if (pointee && cs_type_is_named(pointee) && cs_type_is_basic_struct_or_union(pointee))
            {
                /* Struct pointer store: ptr[i] = val
                 * Stack: [__objectPtr, index, value]
                 * Duplicate value (as assignment result), then call store
                 */
                if (aope != ASSIGN)
                {
                    fprintf(stderr, "compound assignment to struct pointer subscript not supported\n");
                    exit(1);
                }
                /* Generate deep copy for C value semantics */
                cg_emit_struct_deep_copy(cg, pointee);
                /* Duplicate value for assignment result, then store */
                codebuilder_build_dup_x2(cg->builder);
                cg_emit_ptr_store_subscript(cg, array_type);
                handle_for_expression_leave(cg, expr);
                return;
            }

            /* Primitive pointer subscript assignment */
            if (aope != ASSIGN)
            {
                /* Compound assignment to pointer element: ptr[i] += value
                 * Stack: [ptr, index, value]
                 * Need to: load current value, apply operation, store result
                 */
                int value_local = allocate_temp_local(cg);
                codebuilder_build_istore(cg->builder, value_local);

                /* Stack: [ptr, index] - duplicate for store */
                codebuilder_build_dup2(cg->builder);

                /* Stack: [ptr, index, ptr, index] - load current value */
                cg_emit_ptr_subscript(cg, array_type);

                /* Stack: [ptr, index, current_value] - load new value and apply */
                codebuilder_build_iload(cg->builder, value_local);

                switch (aope)
                {
                case ADD_ASSIGN:
                    codebuilder_build_iadd(cg->builder);
                    break;
                case SUB_ASSIGN:
                    codebuilder_build_isub(cg->builder);
                    break;
                case MUL_ASSIGN:
                    codebuilder_build_imul(cg->builder);
                    break;
                case DIV_ASSIGN:
                    codebuilder_build_idiv(cg->builder);
                    break;
                case MOD_ASSIGN:
                    codebuilder_build_irem(cg->builder);
                    break;
                case AND_ASSIGN:
                    codebuilder_build_iand(cg->builder);
                    break;
                case OR_ASSIGN:
                    codebuilder_build_ior(cg->builder);
                    break;
                case XOR_ASSIGN:
                    codebuilder_build_ixor(cg->builder);
                    break;
                case LSHIFT_ASSIGN:
                    codebuilder_build_ishl(cg->builder);
                    break;
                case RSHIFT_ASSIGN:
                    codebuilder_build_ishr(cg->builder);
                    break;
                default:
                    fprintf(stderr, "unsupported compound assignment operator\n");
                    exit(1);
                }

                /* Stack: [ptr, index, result] - duplicate result, then store */
                codebuilder_build_dup_value_x2(cg->builder);
                cg_emit_ptr_store_subscript(cg, array_type);

                handle_for_expression_leave(cg, expr);
                return;
            }

            /* Simple assignment: ptr[i] = value
             * Stack: [ptr, index, value]
             * Duplicate value and call store_subscript
             */
            codebuilder_build_dup_value_x2(cg->builder);
            cg_emit_ptr_store_subscript(cg, array_type);

            handle_for_expression_leave(cg, expr);
            return;
        }

        /* Compound assignment to array element: arr[i] += value
         * Stack before: [..., arrayref, index, value]
         * Need to: load current value, apply operation, store result */
        if (aope != ASSIGN)
        {
            /* Save the new value to a temp local */
            int value_local = allocate_temp_local(cg);
            codebuilder_build_istore(cg->builder, value_local);

            /* Stack: [..., arrayref, index]
             * Duplicate for later store */
            codebuilder_build_dup2(cg->builder);

            /* Stack: [..., arrayref, index, arrayref, index]
             * Load current element value */
            codebuilder_build_iaload(cg->builder);

            /* Stack: [..., arrayref, index, current_value]
             * Load the new value and apply operation */
            codebuilder_build_iload(cg->builder, value_local);

            /* Stack: [..., arrayref, index, current_value, new_value]
             * Apply the compound operation */
            switch (aope)
            {
            case ADD_ASSIGN:
                codebuilder_build_iadd(cg->builder);
                break;
            case SUB_ASSIGN:
                codebuilder_build_isub(cg->builder);
                break;
            case MUL_ASSIGN:
                codebuilder_build_imul(cg->builder);
                break;
            case DIV_ASSIGN:
                codebuilder_build_idiv(cg->builder);
                break;
            case MOD_ASSIGN:
                codebuilder_build_irem(cg->builder);
                break;
            case AND_ASSIGN:
                codebuilder_build_iand(cg->builder);
                break;
            case OR_ASSIGN:
                codebuilder_build_ior(cg->builder);
                break;
            case XOR_ASSIGN:
                codebuilder_build_ixor(cg->builder);
                break;
            case LSHIFT_ASSIGN:
                codebuilder_build_ishl(cg->builder);
                break;
            case RSHIFT_ASSIGN:
                codebuilder_build_ishr(cg->builder);
                break;
            default:
                fprintf(stderr, "unsupported compound assignment operator\n");
                exit(1);
            }

            /* Stack: [..., arrayref, index, result]
             * Duplicate result for expression value, then store */
            codebuilder_build_dup_value_x2(cg->builder);
            codebuilder_build_iastore(cg->builder);

            handle_for_expression_leave(cg, expr);
            return;
        }

        /* For struct element types, generate deep copy for C value semantics.
         * Skip typedef aliases for primitive types (e.g., uint32_t). */
        if (element_type && cs_type_is_named(element_type) && cs_type_is_basic_struct_or_union(element_type))
        {
            cg_emit_struct_deep_copy(cg, element_type);
        }

        /* Duplicate value and insert below array address (arrayref + index) */
        codebuilder_build_dup_value_x2(cg->builder);
        if (element_type && (cs_type_is_array(element_type) ||
                             cs_type_is_pointer(element_type)))
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
            else if (cs_type_is_int_exact(element_type))
            {
                codebuilder_build_iastore(cg->builder);
            }
            else
            {
                /* Named types (struct, typedef like void*) use aastore */
                codebuilder_build_aastore(cg->builder);
            }
        }
        else
        {
            codebuilder_build_iastore(cg->builder);
        }

        handle_for_expression_leave(cg, expr);
        return;
    }

    if (left->kind == MEMBER_EXPRESSION)
    {
        cg->ctx.assign_target = NULL;

        Expression *target = left->u.member_expression.target;
        const char *me_member_name = left->u.member_expression.member_name;

        if (!target || !target->type)
        {
            fprintf(stderr, "member expression target has no type\n");
            exit(1);
        }

        TypeSpecifier *struct_type = target->type;
        if (cs_type_is_pointer(struct_type))
        {
            struct_type = cs_type_child(struct_type);
        }

        const char *class_name = cg_get_struct_class_name(cg, struct_type);
        int class_idx = find_class_index(cg, class_name);

        /* Check for special union handling */
        CS_UnionKind union_kind = cs_union_kind(struct_type);
        StructMember *member = cs_lookup_struct_member(cg->compiler, struct_type, me_member_name);
        TypeSpecifier *field_type = member ? member->type : NULL;

        /* Determine physical field name and whether conversion is needed */
        const char *physical_field_name = me_member_name;
        bool need_float_conversion = false;
        bool need_double_conversion = false;
        bool need_box = false;

        switch (union_kind)
        {
        case CS_UNION_KIND_TYPE_PUNNING_INT_FLOAT:
            physical_field_name = "_bits";
            /* If assigning to float member, need to convert to int bits */
            if (field_type && cs_type_is_float_exact(field_type))
            {
                need_float_conversion = true;
            }
            break;

        case CS_UNION_KIND_TYPE_PUNNING_LONG_DOUBLE:
            physical_field_name = "_bits";
            /* If assigning to double member, need to convert to long bits */
            if (field_type && cs_type_is_double_exact(field_type))
            {
                need_double_conversion = true;
            }
            break;

        case CS_UNION_KIND_REFERENCE:
            physical_field_name = "_ref";
            /* Need to box primitive values (including enums) to Object */
            if (field_type && (cs_type_is_primitive(field_type) || cs_type_is_enum(field_type)))
            {
                need_box = true;
            }
            break;

        default:
            /* TAGGED or NOT_UNION: use original field name */
            break;
        }

        int field_idx = find_field_index(cg, class_idx, physical_field_name);

        /* For special unions, use storage type for field descriptor */
        TypeSpecifier *storage_type = field_type;
        if (union_kind == CS_UNION_KIND_TYPE_PUNNING_INT_FLOAT ||
            union_kind == CS_UNION_KIND_TYPE_PUNNING_LONG_DOUBLE ||
            union_kind == CS_UNION_KIND_REFERENCE)
        {
            storage_type = NULL; /* Use descriptor from class definition */
        }

        int const_idx = cg_find_or_add_struct_field(cg, class_name, physical_field_name,
                                                    field_idx, storage_type);

        if (!simple_assign)
        {
            /* Compound assignment to struct field: ptr->field += value
             * Stack before: [struct_obj, value]
             * Need to: get current field value, apply op, store result */

            /* Determine value tag for the field */
            CF_ValueTag tag = CF_VAL_INT;
            if (field_type)
            {
                if (cs_type_is_long_exact(field_type))
                    tag = CF_VAL_LONG;
                else if (cs_type_is_float_exact(field_type))
                    tag = CF_VAL_FLOAT;
                else if (cs_type_is_double_exact(field_type))
                    tag = CF_VAL_DOUBLE;
            }

            /* Save the right-hand value to temp local */
            int value_local = allocate_temp_local_for_tag(cg, tag);
            switch (tag)
            {
            case CF_VAL_LONG:
                codebuilder_build_lstore(cg->builder, value_local);
                break;
            case CF_VAL_FLOAT:
                codebuilder_build_fstore(cg->builder, value_local);
                break;
            case CF_VAL_DOUBLE:
                codebuilder_build_dstore(cg->builder, value_local);
                break;
            default:
                codebuilder_build_istore(cg->builder, value_local);
                break;
            }
            /* Stack: [struct_obj] */

            codebuilder_build_dup(cg->builder);
            /* Stack: [struct_obj, struct_obj] */

            codebuilder_build_getfield(cg->builder, const_idx);
            /* Stack: [struct_obj, current_value] */

            /* Load the right-hand value */
            switch (tag)
            {
            case CF_VAL_LONG:
                codebuilder_build_lload(cg->builder, value_local);
                break;
            case CF_VAL_FLOAT:
                codebuilder_build_fload(cg->builder, value_local);
                break;
            case CF_VAL_DOUBLE:
                codebuilder_build_dload(cg->builder, value_local);
                break;
            default:
                codebuilder_build_iload(cg->builder, value_local);
                break;
            }
            /* Stack: [struct_obj, current_value, rhs_value] */

            /* Apply the compound operation */
            switch (aope)
            {
            case ADD_ASSIGN:
                if (tag == CF_VAL_LONG)
                    codebuilder_build_ladd(cg->builder);
                else if (tag == CF_VAL_FLOAT)
                    codebuilder_build_fadd(cg->builder);
                else if (tag == CF_VAL_DOUBLE)
                    codebuilder_build_dadd(cg->builder);
                else
                    codebuilder_build_iadd(cg->builder);
                break;
            case SUB_ASSIGN:
                if (tag == CF_VAL_LONG)
                    codebuilder_build_lsub(cg->builder);
                else if (tag == CF_VAL_FLOAT)
                    codebuilder_build_fsub(cg->builder);
                else if (tag == CF_VAL_DOUBLE)
                    codebuilder_build_dsub(cg->builder);
                else
                    codebuilder_build_isub(cg->builder);
                break;
            case MUL_ASSIGN:
                if (tag == CF_VAL_LONG)
                    codebuilder_build_lmul(cg->builder);
                else if (tag == CF_VAL_FLOAT)
                    codebuilder_build_fmul(cg->builder);
                else if (tag == CF_VAL_DOUBLE)
                    codebuilder_build_dmul(cg->builder);
                else
                    codebuilder_build_imul(cg->builder);
                break;
            case DIV_ASSIGN:
                if (tag == CF_VAL_LONG)
                    codebuilder_build_ldiv(cg->builder);
                else if (tag == CF_VAL_FLOAT)
                    codebuilder_build_fdiv(cg->builder);
                else if (tag == CF_VAL_DOUBLE)
                    codebuilder_build_ddiv(cg->builder);
                else
                    codebuilder_build_idiv(cg->builder);
                break;
            case MOD_ASSIGN:
                if (tag == CF_VAL_LONG)
                    codebuilder_build_lrem(cg->builder);
                else
                    codebuilder_build_irem(cg->builder);
                break;
            case AND_ASSIGN:
                if (tag == CF_VAL_LONG)
                    codebuilder_build_land(cg->builder);
                else
                    codebuilder_build_iand(cg->builder);
                break;
            case OR_ASSIGN:
                if (tag == CF_VAL_LONG)
                    codebuilder_build_lor(cg->builder);
                else
                    codebuilder_build_ior(cg->builder);
                break;
            case XOR_ASSIGN:
                if (tag == CF_VAL_LONG)
                    codebuilder_build_lxor(cg->builder);
                else
                    codebuilder_build_ixor(cg->builder);
                break;
            default:
                fprintf(stderr, "unsupported compound assignment operator %d for struct field at %s:%d\n",
                        aope, expr->input_location.path ? expr->input_location.path : "unknown",
                        expr->input_location.line);
                exit(1);
            }
            /* Stack: [struct_obj, new_value] */

            /* Duplicate result below struct_obj for expression value
             * Use semantic API that auto-selects dup_x1 or dup2_x1 */
            codebuilder_build_dup_value_x1(cg->builder);
            /* Stack: [new_value, struct_obj, new_value] */

            codebuilder_build_putfield(cg->builder, const_idx);
            /* Stack: [new_value] */

            handle_for_expression_leave(cg, expr);
            return;
        }

        /* Apply type conversions for special unions before putfield */
        if (need_float_conversion)
        {
            /* float -> int via Float.floatToRawIntBits */
            int method_idx = cp_builder_add_methodref(code_output_cp(cg->output),
                                                      "java/lang/Float", "floatToRawIntBits",
                                                      "(F)I");
            codebuilder_build_invokestatic(cg->builder, method_idx);
        }
        else if (need_double_conversion)
        {
            /* double -> long via Double.doubleToRawLongBits */
            int method_idx = cp_builder_add_methodref(code_output_cp(cg->output),
                                                      "java/lang/Double", "doubleToRawLongBits",
                                                      "(D)J");
            codebuilder_build_invokestatic(cg->builder, method_idx);
        }
        else if (need_box && field_type)
        {
            /* Box primitive value to Object for REFERENCE union
             * Stack: [struct_obj, value] -> [struct_obj, boxed_value]
             */

            /* Box the value based on type */
            if (cs_type_is_long_exact(field_type))
            {
                int method_idx = cp_builder_add_methodref(code_output_cp(cg->output),
                                                          "java/lang/Long", "valueOf", "(J)Ljava/lang/Long;");
                codebuilder_build_invokestatic(cg->builder, method_idx);
            }
            else if (cs_type_is_double_exact(field_type))
            {
                int method_idx = cp_builder_add_methodref(code_output_cp(cg->output),
                                                          "java/lang/Double", "valueOf", "(D)Ljava/lang/Double;");
                codebuilder_build_invokestatic(cg->builder, method_idx);
            }
            else if (cs_type_is_float_exact(field_type))
            {
                int method_idx = cp_builder_add_methodref(code_output_cp(cg->output),
                                                          "java/lang/Float", "valueOf", "(F)Ljava/lang/Float;");
                codebuilder_build_invokestatic(cg->builder, method_idx);
            }
            else /* int, char, short, bool */
            {
                int method_idx = cp_builder_add_methodref(code_output_cp(cg->output),
                                                          "java/lang/Integer", "valueOf", "(I)Ljava/lang/Integer;");
                codebuilder_build_invokestatic(cg->builder, method_idx);
            }
            /* Stack: [struct_obj, boxed_value] */
            /* Fall through to normal putfield below */
        }

        /* For struct field types, generate deep copy for C value semantics.
         * Skip typedef aliases for primitive types (e.g., uint32_t). */
        if (field_type && cs_type_is_named(field_type) && cs_type_is_basic_struct_or_union(field_type))
        {
            cg_emit_struct_deep_copy(cg, field_type);
        }

        /* Duplicate value below struct_obj for expression result
         * Stack: [struct_obj, value] -> [value, struct_obj, value]
         * Use semantic API that auto-selects dup_x1 or dup2_x1 based on value category */
        codebuilder_build_dup_value_x1(cg->builder);
        codebuilder_build_putfield(cg->builder, const_idx);
        handle_for_expression_leave(cg, expr);
        return;
    }

    if (left->kind == DEREFERENCE_EXPRESSION)
    {
        cg->ctx.assign_target = NULL;

        Expression *target = left->u.dereference_expression;
        if (!target || !target->type)
        {
            fprintf(stderr, "dereference assignment target has no type\n");
            exit(1);
        }

        if (!simple_assign)
        {
            /* Compound assignment to dereference: *ptr += value
             * Stack before: [ptr, value]
             * Need to: deref current value, apply op, store result */
            TypeSpecifier *pointee = cs_type_child(target->type);

            /* Check if this is pointer compound assignment: *ptr_ptr += int or *ptr_ptr -= int */
            if (pointee && cs_type_is_pointer(pointee) &&
                (aope == ADD_ASSIGN || aope == SUB_ASSIGN))
            {
                /* Pointer compound assignment through dereference
                 * Stack before: [ptr, int_value]
                 * Need to: deref to get current pointer, add/sub offset, store result */

                /* Save the right-hand value (int offset) to temp local */
                int value_local = allocate_temp_local(cg);
                codebuilder_build_istore(cg->builder, value_local);
                /* Stack: [ptr] */

                codebuilder_build_dup(cg->builder);
                /* Stack: [ptr, ptr] */

                cg_emit_ptr_deref(cg, target->type);
                /* Stack: [ptr, Object] (Object is the dereferenced pointer) */

                /* Checkcast Object -> appropriate pointer type */
                cg_emit_checkcast_for_pointer_type(cg, pointee);
                /* Stack: [ptr, __XPtr] (properly typed pointer) */

                /* Load the int offset */
                codebuilder_build_iload(cg->builder, value_local);
                /* Stack: [ptr, current_ptr_value, int_offset] */

                if (aope == SUB_ASSIGN)
                {
                    /* For subtraction, negate the offset */
                    codebuilder_build_ineg(cg->builder);
                }

                /* Add offset to pointer */
                cg_emit_ptr_add(cg, pointee);
                /* Stack: [ptr, new_ptr_value] */

                /* Duplicate result for expression value */
                codebuilder_build_dup_x1(cg->builder);
                /* Stack: [new_ptr_value, ptr, new_ptr_value] */

                cg_emit_ptr_store(cg, target->type);
                /* Stack: [new_ptr_value] */

                handle_for_expression_leave(cg, expr);
                return;
            }

            /* Determine value tag for the pointee type */
            CF_ValueTag tag = CF_VAL_INT;
            if (pointee)
            {
                if (cs_type_is_long_exact(pointee))
                    tag = CF_VAL_LONG;
                else if (cs_type_is_float_exact(pointee))
                    tag = CF_VAL_FLOAT;
                else if (cs_type_is_double_exact(pointee))
                    tag = CF_VAL_DOUBLE;
            }

            /* Save the right-hand value to temp local */
            int value_local = allocate_temp_local_for_tag(cg, tag);
            switch (tag)
            {
            case CF_VAL_LONG:
                codebuilder_build_lstore(cg->builder, value_local);
                break;
            case CF_VAL_FLOAT:
                codebuilder_build_fstore(cg->builder, value_local);
                break;
            case CF_VAL_DOUBLE:
                codebuilder_build_dstore(cg->builder, value_local);
                break;
            default:
                codebuilder_build_istore(cg->builder, value_local);
                break;
            }
            /* Stack: [ptr] */

            codebuilder_build_dup(cg->builder);
            /* Stack: [ptr, ptr] */

            cg_emit_ptr_deref(cg, target->type);
            /* Stack: [ptr, current_value] */

            /* Load the right-hand value */
            switch (tag)
            {
            case CF_VAL_LONG:
                codebuilder_build_lload(cg->builder, value_local);
                break;
            case CF_VAL_FLOAT:
                codebuilder_build_fload(cg->builder, value_local);
                break;
            case CF_VAL_DOUBLE:
                codebuilder_build_dload(cg->builder, value_local);
                break;
            default:
                codebuilder_build_iload(cg->builder, value_local);
                break;
            }
            /* Stack: [ptr, current_value, rhs_value] */

            /* Apply the compound operation */
            switch (aope)
            {
            case ADD_ASSIGN:
                if (tag == CF_VAL_LONG)
                    codebuilder_build_ladd(cg->builder);
                else if (tag == CF_VAL_FLOAT)
                    codebuilder_build_fadd(cg->builder);
                else if (tag == CF_VAL_DOUBLE)
                    codebuilder_build_dadd(cg->builder);
                else
                    codebuilder_build_iadd(cg->builder);
                break;
            case SUB_ASSIGN:
                if (tag == CF_VAL_LONG)
                    codebuilder_build_lsub(cg->builder);
                else if (tag == CF_VAL_FLOAT)
                    codebuilder_build_fsub(cg->builder);
                else if (tag == CF_VAL_DOUBLE)
                    codebuilder_build_dsub(cg->builder);
                else
                    codebuilder_build_isub(cg->builder);
                break;
            case MUL_ASSIGN:
                if (tag == CF_VAL_LONG)
                    codebuilder_build_lmul(cg->builder);
                else if (tag == CF_VAL_FLOAT)
                    codebuilder_build_fmul(cg->builder);
                else if (tag == CF_VAL_DOUBLE)
                    codebuilder_build_dmul(cg->builder);
                else
                    codebuilder_build_imul(cg->builder);
                break;
            case DIV_ASSIGN:
                if (tag == CF_VAL_LONG)
                    codebuilder_build_ldiv(cg->builder);
                else if (tag == CF_VAL_FLOAT)
                    codebuilder_build_fdiv(cg->builder);
                else if (tag == CF_VAL_DOUBLE)
                    codebuilder_build_ddiv(cg->builder);
                else
                    codebuilder_build_idiv(cg->builder);
                break;
            case MOD_ASSIGN:
                if (tag == CF_VAL_LONG)
                    codebuilder_build_lrem(cg->builder);
                else
                    codebuilder_build_irem(cg->builder);
                break;
            case AND_ASSIGN:
                if (tag == CF_VAL_LONG)
                    codebuilder_build_land(cg->builder);
                else
                    codebuilder_build_iand(cg->builder);
                break;
            case OR_ASSIGN:
                if (tag == CF_VAL_LONG)
                    codebuilder_build_lor(cg->builder);
                else
                    codebuilder_build_ior(cg->builder);
                break;
            case XOR_ASSIGN:
                if (tag == CF_VAL_LONG)
                    codebuilder_build_lxor(cg->builder);
                else
                    codebuilder_build_ixor(cg->builder);
                break;
            case LSHIFT_ASSIGN:
                if (tag == CF_VAL_LONG)
                    codebuilder_build_lshl(cg->builder);
                else
                    codebuilder_build_ishl(cg->builder);
                break;
            case RSHIFT_ASSIGN:
                if (tag == CF_VAL_LONG)
                    codebuilder_build_lshr(cg->builder);
                else
                    codebuilder_build_ishr(cg->builder);
                break;
            default:
                fprintf(stderr, "unsupported compound assignment operator for dereference\n");
                exit(1);
            }
            /* Stack: [ptr, new_value] */

            /* Duplicate result below ptr for expression value */
            codebuilder_build_dup_value_x1(cg->builder);
            /* Stack: [new_value, ptr, new_value] */

            cg_emit_ptr_store(cg, target->type);
            /* Stack: [new_value] */
            handle_for_expression_leave(cg, expr);
            return;
        }

        /* Simple assignment: *ptr = value
         * Stack at this point: [ptr, value]
         */

        /* For struct types, generate deep copy for C value semantics.
         * In Java, without deep copy, assignment stores reference and
         * multiple stack entries would share the same object. */
        TypeSpecifier *pointee = cs_type_child(target->type);
        if (pointee && cs_type_is_named(pointee) && cs_type_is_basic_struct_or_union(pointee))
        {
            cg_emit_struct_deep_copy(cg, pointee);
        }

        /* Duplicate value below ptr for expression result */
        codebuilder_build_dup_value_x1(cg->builder);
        /* Stack: [value, ptr, value] */
        cg_emit_ptr_store(cg, target->type);
        /* Stack: [value] */
        handle_for_expression_leave(cg, expr);
        return;
    }

    if (left->kind == IDENTIFIER_EXPRESSION)
    {
        cg->ctx.assign_target = NULL;

        Declaration *decl = left->u.identifier.u.declaration;
        if (!decl)
        {
            fprintf(stderr, "identifier declaration missing in codegen\n");
            exit(1);
        }

        CodegenSymbolInfo sym = cg_ensure_symbol(cg, decl);
        CF_ValueTag tag = cg_decl_value_tag(decl);

        /* Compound assignment to identifier: var += value
         * Stack before: [..., value]
         * Need to: load current var, apply operation, store result */
        if (aope != ASSIGN)
        {
            /* Check for pointer compound assignment: ptr += int or ptr -= int */
            if ((aope == ADD_ASSIGN || aope == SUB_ASSIGN) &&
                cs_type_is_pointer(decl->type))
            {
                /* Pointer compound assignment: ptr += int or ptr -= int
                 * Stack: [int_value] */
                int int_local = allocate_temp_local(cg);
                codebuilder_build_istore(cg->builder, int_local);
                /* Stack: [] */

                if (decl->needs_heap_lift && sym.kind != CG_SYMBOL_STATIC)
                {
                    /* Heap-lifted pointer: boxed in Object[] array.
                     * Load box, load box[0], add int, store back to box[0]. */
                    codebuilder_build_aload(cg->builder, sym.index);
                    /* Stack: [box] */
                    codebuilder_build_dup(cg->builder);
                    /* Stack: [box, box] */
                    codebuilder_build_iconst(cg->builder, 0);
                    /* Stack: [box, box, 0] */
                    codebuilder_build_aaload(cg->builder);
                    /* Stack: [box, val] */
                    cg_emit_checkcast_for_pointer_type(cg, decl->type);
                    /* Stack: [box, ptr] */

                    /* Load int value and add */
                    codebuilder_build_iload(cg->builder, int_local);
                    if (aope == SUB_ASSIGN)
                    {
                        codebuilder_build_ineg(cg->builder);
                    }
                    cg_emit_ptr_add(cg, decl->type);
                    /* Stack: [box, new_ptr] */

                    /* Duplicate for expression value */
                    codebuilder_build_dup_x1(cg->builder);
                    /* Stack: [new_ptr, box, new_ptr] */

                    /* Store back to box[0] */
                    codebuilder_build_iconst(cg->builder, 0);
                    codebuilder_build_swap(cg->builder);
                    codebuilder_build_aastore(cg->builder);
                    /* Stack: [new_ptr] */
                }
                else
                {
                    /* Load current pointer value */
                    if (sym.kind == CG_SYMBOL_STATIC)
                    {
                        int pool_idx = cg_find_or_add_field(cg, decl);
                        codebuilder_build_getstatic(cg->builder, pool_idx);
                    }
                    else
                    {
                        codebuilder_build_aload(cg->builder, sym.index);
                    }
                    /* Stack: [ptr] */

                    /* Load int value */
                    codebuilder_build_iload(cg->builder, int_local);
                    /* Stack: [ptr, int] */

                    /* For subtraction, negate the int */
                    if (aope == SUB_ASSIGN)
                    {
                        codebuilder_build_ineg(cg->builder);
                    }

                    /* Call ptr_add */
                    cg_emit_ptr_add(cg, decl->type);
                    /* Stack: [new_ptr] */

                    /* Duplicate for expression value */
                    codebuilder_build_dup(cg->builder);
                    /* Stack: [new_ptr, new_ptr] */

                    /* Store result */
                    if (sym.kind == CG_SYMBOL_STATIC)
                    {
                        int pool_idx = cg_find_or_add_field(cg, decl);
                        codebuilder_build_putstatic(cg->builder, pool_idx);
                    }
                    else
                    {
                        codebuilder_build_astore(cg->builder, sym.index);
                    }
                    /* Stack: [new_ptr] */
                }

                handle_for_expression_leave(cg, expr);
                return;
            }

            /* Save the new value to a temp local */
            int value_local = allocate_temp_local_for_tag(cg, tag);
            switch (tag)
            {
            case CF_VAL_INT:
                codebuilder_build_istore(cg->builder, value_local);
                break;
            case CF_VAL_LONG:
                codebuilder_build_lstore(cg->builder, value_local);
                break;
            case CF_VAL_FLOAT:
                codebuilder_build_fstore(cg->builder, value_local);
                break;
            case CF_VAL_DOUBLE:
                codebuilder_build_dstore(cg->builder, value_local);
                break;
            default:
                fprintf(stderr, "compound assignment for reference type not supported\n");
                exit(1);
            }

            /* Load current variable value */
            if (sym.kind == CG_SYMBOL_STATIC)
            {
                int pool_idx = cg_find_or_add_field(cg, decl);
                codebuilder_build_getstatic(cg->builder, pool_idx);
            }
            else
            {
                switch (tag)
                {
                case CF_VAL_INT:
                    codebuilder_build_iload(cg->builder, sym.index);
                    break;
                case CF_VAL_LONG:
                    codebuilder_build_lload(cg->builder, sym.index);
                    break;
                case CF_VAL_FLOAT:
                    codebuilder_build_fload(cg->builder, sym.index);
                    break;
                case CF_VAL_DOUBLE:
                    codebuilder_build_dload(cg->builder, sym.index);
                    break;
                default:
                    break;
                }
            }

            /* Load the right-hand value */
            switch (tag)
            {
            case CF_VAL_INT:
                codebuilder_build_iload(cg->builder, value_local);
                break;
            case CF_VAL_LONG:
                codebuilder_build_lload(cg->builder, value_local);
                break;
            case CF_VAL_FLOAT:
                codebuilder_build_fload(cg->builder, value_local);
                break;
            case CF_VAL_DOUBLE:
                codebuilder_build_dload(cg->builder, value_local);
                break;
            default:
                break;
            }

            /* Apply the compound operation */
            switch (aope)
            {
            case ADD_ASSIGN:
                if (tag == CF_VAL_INT)
                    codebuilder_build_iadd(cg->builder);
                else if (tag == CF_VAL_LONG)
                    codebuilder_build_ladd(cg->builder);
                else if (tag == CF_VAL_FLOAT)
                    codebuilder_build_fadd(cg->builder);
                else if (tag == CF_VAL_DOUBLE)
                    codebuilder_build_dadd(cg->builder);
                break;
            case SUB_ASSIGN:
                if (tag == CF_VAL_INT)
                    codebuilder_build_isub(cg->builder);
                else if (tag == CF_VAL_LONG)
                    codebuilder_build_lsub(cg->builder);
                else if (tag == CF_VAL_FLOAT)
                    codebuilder_build_fsub(cg->builder);
                else if (tag == CF_VAL_DOUBLE)
                    codebuilder_build_dsub(cg->builder);
                break;
            case MUL_ASSIGN:
                if (tag == CF_VAL_INT)
                    codebuilder_build_imul(cg->builder);
                else if (tag == CF_VAL_LONG)
                    codebuilder_build_lmul(cg->builder);
                else if (tag == CF_VAL_FLOAT)
                    codebuilder_build_fmul(cg->builder);
                else if (tag == CF_VAL_DOUBLE)
                    codebuilder_build_dmul(cg->builder);
                break;
            case DIV_ASSIGN:
                if (tag == CF_VAL_INT)
                    codebuilder_build_idiv(cg->builder);
                else if (tag == CF_VAL_LONG)
                    codebuilder_build_ldiv(cg->builder);
                else if (tag == CF_VAL_FLOAT)
                    codebuilder_build_fdiv(cg->builder);
                else if (tag == CF_VAL_DOUBLE)
                    codebuilder_build_ddiv(cg->builder);
                break;
            case MOD_ASSIGN:
                if (tag == CF_VAL_INT)
                    codebuilder_build_irem(cg->builder);
                else if (tag == CF_VAL_LONG)
                    codebuilder_build_lrem(cg->builder);
                else if (tag == CF_VAL_FLOAT)
                    codebuilder_build_frem(cg->builder);
                else if (tag == CF_VAL_DOUBLE)
                    codebuilder_build_drem(cg->builder);
                break;
            case AND_ASSIGN:
                if (tag == CF_VAL_INT)
                    codebuilder_build_iand(cg->builder);
                else if (tag == CF_VAL_LONG)
                    codebuilder_build_land(cg->builder);
                break;
            case OR_ASSIGN:
                if (tag == CF_VAL_INT)
                    codebuilder_build_ior(cg->builder);
                else if (tag == CF_VAL_LONG)
                    codebuilder_build_lor(cg->builder);
                break;
            case XOR_ASSIGN:
                if (tag == CF_VAL_INT)
                    codebuilder_build_ixor(cg->builder);
                else if (tag == CF_VAL_LONG)
                    codebuilder_build_lxor(cg->builder);
                break;
            case LSHIFT_ASSIGN:
                if (tag == CF_VAL_INT)
                    codebuilder_build_ishl(cg->builder);
                else if (tag == CF_VAL_LONG)
                    codebuilder_build_lshl(cg->builder);
                break;
            case RSHIFT_ASSIGN:
                if (tag == CF_VAL_INT)
                    codebuilder_build_ishr(cg->builder);
                else if (tag == CF_VAL_LONG)
                    codebuilder_build_lshr(cg->builder);
                break;
            default:
                fprintf(stderr, "unsupported compound assignment operator\n");
                exit(1);
            }

            /* Duplicate result for expression value, then store */
            codebuilder_build_dup_value(cg->builder);

            if (sym.kind == CG_SYMBOL_STATIC)
            {
                int pool_idx = cg_find_or_add_field(cg, decl);
                codebuilder_build_putstatic(cg->builder, pool_idx);
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
                default:
                    break;
                }
            }

            handle_for_expression_leave(cg, expr);
            return;
        }

        /* Simple assignment */
        /* For struct types, generate deep copy for C value semantics.
         * Skip typedef aliases for primitive types (e.g., uint32_t). */
        TypeSpecifier *decl_type = decl->type;
        if (decl_type && cs_type_is_named(decl_type) && cs_type_is_basic_struct_or_union(decl_type))
        {
            cg_emit_struct_deep_copy(cg, decl_type);
        }

        /* Handle heap-lifted variable assignment */
        if (decl->needs_heap_lift && sym.kind != CG_SYMBOL_STATIC)
        {
            /* Stack: [array_ref, value] (array_ref was loaded in leave_identifierexpr)
             * Need to end with [value] as expression result, and store value into array[0]
             * Rearrange stack and store:
             * 1-slot: [array_ref, value] -> [value, array_ref, value] -> [value, array_ref, 0, value] -> iastore -> [value]
             * 2-slot: similar with dup2_x1 */
            CF_ValueTag actual_tag = cg_to_value_tag(decl->type);

            if (actual_tag == CF_VAL_LONG || actual_tag == CF_VAL_DOUBLE)
            {
                /* For 2-slot values: value occupies 2 slots */
                /* Stack: [array_ref, value(2)] */
                codebuilder_build_dup2_x1(cg->builder);
                /* Stack: [value(2), array_ref, value(2)] */
                codebuilder_build_iconst(cg->builder, 0);
                /* Stack: [value(2), array_ref, value(2), 0] */
                codebuilder_build_dup_x2(cg->builder);
                /* Stack: [value(2), array_ref, 0, value(2), 0] */
                codebuilder_build_pop(cg->builder);
                /* Stack: [value(2), array_ref, 0, value(2)] */
            }
            else
            {
                /* For 1-slot values */
                /* Stack: [array_ref, value] */
                codebuilder_build_dup_x1(cg->builder);
                /* Stack: [value, array_ref, value] */
                codebuilder_build_iconst(cg->builder, 0);
                /* Stack: [value, array_ref, value, 0] */
                codebuilder_build_swap(cg->builder);
                /* Stack: [value, array_ref, 0, value] */
            }

            /* Now store into array[0] */
            switch (actual_tag)
            {
            case CF_VAL_INT:
                codebuilder_build_iastore(cg->builder);
                break;
            case CF_VAL_LONG:
                codebuilder_build_lastore(cg->builder);
                break;
            case CF_VAL_FLOAT:
                codebuilder_build_fastore(cg->builder);
                break;
            case CF_VAL_DOUBLE:
                codebuilder_build_dastore(cg->builder);
                break;
            default:
                codebuilder_build_aastore(cg->builder);
                break;
            }
            /* Stack: [value] - the duplicated value as expression result */
            handle_for_expression_leave(cg, expr);
            return;
        }

        /* For pointer types assigned from another pointer variable,
         * we need to clone the pointer wrapper to avoid Java reference aliasing.
         * In C: p = q; keeps p and q as independent pointers.
         * In Java without clone: p and q would be the same object reference. */
        bool needs_clone = false;
        if (cs_type_is_pointer(decl->type) && right && right->type &&
            cs_type_is_pointer(right->type) &&
            right->kind == IDENTIFIER_EXPRESSION)
        {
            needs_clone = true;
        }

        if (needs_clone)
        {
            /* Clone the pointer before duplicating for expression result */
            cg_emit_ptr_clone(cg, decl->type);
        }

        /* Duplicate value for expression result (assignment expression returns assigned value) */
        codebuilder_build_dup_value(cg->builder);

        if (sym.kind == CG_SYMBOL_STATIC)
        {
            int pool_idx = cg_find_or_add_field(cg, decl);
            codebuilder_build_putstatic(cg->builder, pool_idx);
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
                fprintf(stderr, "leave_assignexpr: invalid tag %d for local %d\n", tag, sym.index);
                exit(1);
            }
        }
        handle_for_expression_leave(cg, expr);
        return;
    }

    fprintf(stderr, "unsupported assignment target kind %d\n", left->kind);
    exit(1);
}

void enter_incexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    mark_for_condition_start(cg, expr);
    /* Mark the target expression so leave_identifierexpr skips loading */
    cg->ctx.inc_target = expr->u.inc_dec.target;
}

static Declaration *get_inc_target_decl(Expression *target)
{
    if (!target)
    {
        return NULL;
    }
    if (target->kind == IDENTIFIER_EXPRESSION)
    {
        return target->u.identifier.u.declaration;
    }
    /* For array/member expressions, get the underlying identifier */
    if (target->kind == ARRAY_EXPRESSION)
    {
        return get_inc_target_decl(target->u.array_expression.array);
    }
    if (target->kind == MEMBER_EXPRESSION)
    {
        return get_inc_target_decl(target->u.member_expression.target);
    }
    return NULL;
}

void leave_incexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    /* Clear the increment target flag */
    cg->ctx.inc_target = NULL;
    Expression *target = expr->u.inc_dec.target;
    bool is_prefix = expr->u.inc_dec.is_prefix;
    bool is_decrement = (expr->kind == DECREMENT_EXPRESSION);
    if (!target)
    {
        fprintf(stderr, "invalid increment target: null\n");
        exit(1);
    }

    /* Handle simple identifier increment (most common case) */
    if (target->kind == IDENTIFIER_EXPRESSION)
    {
        Declaration *decl = target->u.identifier.u.declaration;
        if (!decl)
        {
            fprintf(stderr, "invalid increment target: no declaration\n");
            exit(1);
        }

        CodegenSymbolInfo sym = cg_ensure_symbol(cg, decl);
        CF_ValueTag tag = cg_decl_value_tag(decl);
        TypeSpecifier *decl_type = decl->type;

        if (sym.kind == CG_SYMBOL_STATIC)
        {
            int pool_idx = cg_find_or_add_field(cg, decl);
            codebuilder_build_getstatic(cg->builder, pool_idx);

            if (cs_type_is_double_exact(decl_type))
            {
                codebuilder_build_dconst(cg->builder, 1.0);
            }
            else if (cs_type_is_float_exact(decl_type))
            {
                codebuilder_build_fconst(cg->builder, 1.0f);
            }
            else if (cs_type_is_long_exact(decl_type))
            {
                codebuilder_build_lconst(cg->builder, 1);
            }
            else
            {
                codebuilder_build_iconst(cg->builder, 1);
            }

            if (cs_type_is_double_exact(decl_type))
            {
                if (is_decrement)
                    codebuilder_build_dsub(cg->builder);
                else
                    codebuilder_build_dadd(cg->builder);
            }
            else if (cs_type_is_float_exact(decl_type))
            {
                if (is_decrement)
                    codebuilder_build_fsub(cg->builder);
                else
                    codebuilder_build_fadd(cg->builder);
            }
            else if (cs_type_is_long_exact(decl_type))
            {
                if (is_decrement)
                    codebuilder_build_lsub(cg->builder);
                else
                    codebuilder_build_ladd(cg->builder);
            }
            else if (cs_type_is_integral(decl_type) || cs_type_is_bool(decl_type))
            {
                if (is_decrement)
                    codebuilder_build_isub(cg->builder);
                else
                    codebuilder_build_iadd(cg->builder);
            }
            else if (cs_type_is_pointer(decl_type))
            {
                /* Pointer increment/decrement: use inline ptr_add
                 * Stack: [ptr, 1] */
                if (is_decrement)
                {
                    /* For decrement, negate the offset */
                    codebuilder_build_ineg(cg->builder);
                }
                cg_emit_ptr_add(cg, decl_type);
                /* Stack: [new_ptr] */
            }
            else
            {
                fprintf(stderr, "unsupported increment operand type: kind=%d, decl=%s\n", cs_type_kind(decl_type), decl->name ? decl->name : "(null)");
                exit(1);
            }

            /* Duplicate the result value for assignment expression */
            codebuilder_build_dup_value(cg->builder);

            codebuilder_build_putstatic(cg->builder, pool_idx);
        }
        else if (decl->needs_heap_lift && cs_type_is_pointer(decl_type))
        {
            /* Heap-lifted pointer variable: boxed in Object[] array.
             * For p++: load box[0], add 1, store back to box[0].
             * Stack operations:
             *   aload box        -> [box]
             *   dup              -> [box, box]
             *   iconst 0         -> [box, box, 0]
             *   aaload           -> [box, val]
             *   checkcast        -> [box, ptr]
             *   (postfix: dup_x1 -> [ptr, box, ptr])
             *   iconst 1         -> [..., ptr, 1]
             *   ptr_add          -> [..., new_ptr]
             *   (prefix: dup_x1  -> [new_ptr, box, new_ptr])
             *   iconst 0         -> [..., box, new_ptr, 0]
             *   swap             -> [..., box, 0, new_ptr]
             *   aastore          -> [result]
             */

            /* Load box array */
            codebuilder_build_aload(cg->builder, sym.index);
            /* Stack: [box] */
            codebuilder_build_dup(cg->builder);
            /* Stack: [box, box] */
            codebuilder_build_iconst(cg->builder, 0);
            /* Stack: [box, box, 0] */
            codebuilder_build_aaload(cg->builder);
            /* Stack: [box, val] */
            cg_emit_checkcast_for_pointer_type(cg, decl_type);
            /* Stack: [box, ptr] */

            if (!is_prefix)
            {
                /* Postfix: duplicate old value below box for return */
                codebuilder_build_dup_x1(cg->builder);
                /* Stack: [ptr, box, ptr] */
            }

            /* Add/subtract 1 */
            codebuilder_build_iconst(cg->builder, 1);
            if (is_decrement)
            {
                codebuilder_build_ineg(cg->builder);
            }
            cg_emit_ptr_add(cg, decl_type);
            /* Stack: postfix=[ptr, box, new_ptr], prefix=[box, new_ptr] */

            if (is_prefix)
            {
                /* Prefix: duplicate new value below box for return */
                codebuilder_build_dup_x1(cg->builder);
                /* Stack: [new_ptr, box, new_ptr] */
            }

            /* Store new value back to box[0] */
            codebuilder_build_iconst(cg->builder, 0);
            /* Stack: [..., box, new_ptr, 0] */
            codebuilder_build_swap(cg->builder);
            /* Stack: [..., box, 0, new_ptr] */
            codebuilder_build_aastore(cg->builder);
            /* Stack: [result] */
        }
        else
        {
            /* Load current value */
            switch (tag)
            {
            case CF_VAL_INT:
                codebuilder_build_iload(cg->builder, sym.index);
                break;
            case CF_VAL_LONG:
                codebuilder_build_lload(cg->builder, sym.index);
                break;
            case CF_VAL_FLOAT:
                codebuilder_build_fload(cg->builder, sym.index);
                break;
            case CF_VAL_DOUBLE:
                codebuilder_build_dload(cg->builder, sym.index);
                break;
            case CF_VAL_OBJECT:
            case CF_VAL_NULL:
                codebuilder_build_aload(cg->builder, sym.index);
                break;
            default:
                fprintf(stderr, "leave_incexpr load: invalid tag %d for local %d\n", tag, sym.index);
                exit(1);
            }

            /* For postfix: duplicate OLD value before adding (return old value) */
            if (!is_prefix)
            {
                codebuilder_build_dup_value(cg->builder);
            }

            /* Add/subtract 1 */
            if (cs_type_is_double_exact(decl_type))
            {
                codebuilder_build_dconst(cg->builder, 1.0);
            }
            else if (cs_type_is_float_exact(decl_type))
            {
                codebuilder_build_fconst(cg->builder, 1.0f);
            }
            else if (cs_type_is_long_exact(decl_type))
            {
                codebuilder_build_lconst(cg->builder, 1);
            }
            else
            {
                codebuilder_build_iconst(cg->builder, 1);
            }

            if (cs_type_is_double_exact(decl_type))
            {
                if (is_decrement)
                    codebuilder_build_dsub(cg->builder);
                else
                    codebuilder_build_dadd(cg->builder);
            }
            else if (cs_type_is_float_exact(decl_type))
            {
                if (is_decrement)
                    codebuilder_build_fsub(cg->builder);
                else
                    codebuilder_build_fadd(cg->builder);
            }
            else if (cs_type_is_long_exact(decl_type))
            {
                if (is_decrement)
                    codebuilder_build_lsub(cg->builder);
                else
                    codebuilder_build_ladd(cg->builder);
            }
            else if (cs_type_is_integral(decl_type) || cs_type_is_bool(decl_type))
            {
                if (is_decrement)
                    codebuilder_build_isub(cg->builder);
                else
                    codebuilder_build_iadd(cg->builder);
            }
            else if (cs_type_is_pointer(decl_type))
            {
                /* Pointer increment/decrement: use inline ptr_add
                 * Stack: [ptr, 1] */
                if (is_decrement)
                {
                    /* For decrement, negate the offset */
                    codebuilder_build_ineg(cg->builder);
                }
                cg_emit_ptr_add(cg, decl_type);
                /* Stack: [new_ptr] */
            }
            else
            {
                fprintf(stderr, "unsupported increment operand type: kind=%d, decl=%s\n", cs_type_kind(decl_type), decl->name ? decl->name : "(null)");
                exit(1);
            }

            /* For prefix: duplicate NEW value after adding (return new value)
             * For postfix: stack is [old_value, new_value], store new_value, old_value remains */
            if (is_prefix)
            {
                codebuilder_build_dup_value(cg->builder);
            }

            /* Store new value */
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
                fprintf(stderr, "leave_incexpr store: invalid tag %d for local %d\n", tag, sym.index);
                exit(1);
            }
        }
    } /* end if IDENTIFIER_EXPRESSION */
    else if (target->kind == MEMBER_EXPRESSION)
    {
        /* Handle member expression increment: ptr->field++ or obj.field++ */
        Expression *struct_target = target->u.member_expression.target;
        const char *me_member_name = target->u.member_expression.member_name;
        bool me_via_pointer = target->u.member_expression.via_pointer;

        if (!struct_target || !struct_target->type)
        {
            fprintf(stderr, "member expression target has no type\n");
            exit(1);
        }

        TypeSpecifier *struct_type = struct_target->type;
        if (me_via_pointer && cs_type_is_pointer(struct_type))
        {
            struct_type = cs_type_child(struct_type);
        }
        else if (cs_type_is_pointer(struct_type))
        {
            struct_type = cs_type_child(struct_type);
        }

        const char *class_name = cg_get_struct_class_name(cg, struct_type);
        if (!class_name)
        {
            fprintf(stderr, "struct type has no name for increment\n");
            exit(1);
        }

        int class_idx = find_class_index(cg, class_name);
        if (class_idx == -1)
        {
            fprintf(stderr, "struct '%s' not found for increment\n", class_name);
            exit(1);
        }

        int field_idx = find_field_index(cg, class_idx, me_member_name);
        if (field_idx == -1)
        {
            fprintf(stderr, "field '%s' not found in struct '%s' for increment\n",
                    me_member_name, class_name);
            exit(1);
        }

        StructMember *member = cs_lookup_struct_member(cg->compiler, struct_type, me_member_name);
        TypeSpecifier *field_type = member ? member->type : NULL;

        int const_idx = cg_find_or_add_struct_field(cg, class_name, me_member_name,
                                                    field_idx, field_type);

        /* Stack: [struct_object] */
        codebuilder_build_dup(cg->builder);
        /* Stack: [struct_object, struct_object] */
        codebuilder_build_getfield(cg->builder, const_idx);
        /* Stack: [struct_object, old_value] */

        /* For postfix: duplicate OLD value before adding (return old value) */
        if (!is_prefix)
        {
            codebuilder_build_dup_value_x1(cg->builder);
            /* Stack: [old_value, struct_object, old_value] */
        }

        /* Add/subtract 1 based on field type */
        if (field_type && cs_type_is_long_exact(field_type))
        {
            codebuilder_build_lconst(cg->builder, 1);
            if (is_decrement)
                codebuilder_build_lsub(cg->builder);
            else
                codebuilder_build_ladd(cg->builder);
        }
        else
        {
            codebuilder_build_iconst(cg->builder, 1);
            if (is_decrement)
                codebuilder_build_isub(cg->builder);
            else
                codebuilder_build_iadd(cg->builder);
        }
        /* Stack: postfix=[old_value, struct_object, new_value], prefix=[struct_object, new_value] */

        /* For prefix: duplicate NEW value below struct_obj for expression value */
        if (is_prefix)
        {
            codebuilder_build_dup_value_x1(cg->builder);
            /* Stack: [new_value, struct_object, new_value] */
        }

        codebuilder_build_putfield(cg->builder, const_idx);
        /* Stack: [result_value] (old for postfix, new for prefix) */
    }
    else if (target->kind == DEREFERENCE_EXPRESSION)
    {
        /* Handle dereference increment: (*ptr)++ or ++(*ptr) */
        Expression *ptr_expr = target->u.dereference_expression;
        if (!ptr_expr || !ptr_expr->type)
        {
            fprintf(stderr, "dereference increment target has no type\n");
            exit(1);
        }

        TypeSpecifier *ptr_type = ptr_expr->type;
        TypeSpecifier *pointee_type = cs_type_child(ptr_type);
        /* Stack: [ptr] */

        codebuilder_build_dup(cg->builder);
        /* Stack: [ptr, ptr] */

        cg_emit_ptr_deref(cg, ptr_type);
        /* Stack: [ptr, value] */

        /* For pointer types, add checkcast before dup to ensure proper type */
        if (pointee_type && cs_type_is_pointer(pointee_type))
        {
            cg_emit_checkcast_for_pointer_type(cg, pointee_type);
        }
        /* Stack: [ptr, typed_value] */

        /* For postfix: duplicate OLD value before adding (return old value) */
        if (!is_prefix)
        {
            codebuilder_build_dup_value_x1(cg->builder);
            /* Stack: [old_value, ptr, old_value] */
        }

        /* Add/subtract 1 based on pointee type */
        if (pointee_type && cs_type_is_long_exact(pointee_type))
        {
            codebuilder_build_lconst(cg->builder, 1);
            if (is_decrement)
                codebuilder_build_lsub(cg->builder);
            else
                codebuilder_build_ladd(cg->builder);
        }
        else if (pointee_type && cs_type_is_double_exact(pointee_type))
        {
            codebuilder_build_dconst(cg->builder, 1.0);
            if (is_decrement)
                codebuilder_build_dsub(cg->builder);
            else
                codebuilder_build_dadd(cg->builder);
        }
        else if (pointee_type && cs_type_is_float_exact(pointee_type))
        {
            codebuilder_build_fconst(cg->builder, 1.0f);
            if (is_decrement)
                codebuilder_build_fsub(cg->builder);
            else
                codebuilder_build_fadd(cg->builder);
        }
        else if (pointee_type && cs_type_is_pointer(pointee_type))
        {
            /* Pointer value increment/decrement: use __ptr_add_X(ptr, 1/-1) */
            codebuilder_build_iconst(cg->builder, 1);
            if (is_decrement)
            {
                codebuilder_build_ineg(cg->builder);
            }
            cg_emit_ptr_add(cg, pointee_type);
        }
        else
        {
            codebuilder_build_iconst(cg->builder, 1);
            if (is_decrement)
                codebuilder_build_isub(cg->builder);
            else
                codebuilder_build_iadd(cg->builder);
        }

        /* For prefix: duplicate NEW value after adding (return new value)
         * Stack after add: postfix [old_value, ptr, new_value], prefix [ptr, new_value] */
        if (is_prefix)
        {
            codebuilder_build_dup_value_x1(cg->builder);
            /* Stack: [new_value, ptr, new_value] */
        }

        cg_emit_ptr_store(cg, ptr_type);
        /* Stack: [new_value] */
    }
    else
    {
        fprintf(stderr, "unsupported increment target kind: %d\n", target->kind);
        exit(1);
    }

    handle_for_expression_leave(cg, expr);
}

void enter_addrexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    mark_for_condition_start(cg, expr);

    Expression *target = expr->u.address_expression;
    if (!target)
    {
        fprintf(stderr, "address target missing\n");
        exit(1);
    }

    /* If taking address of array element, mark it so leave_arrayexpr won't load value */
    if (target->kind == ARRAY_EXPRESSION)
    {
        cg->ctx.addr_target = target;
    }

    /* If taking address of heap-lifted identifier, mark it so leave_identifierexpr
     * won't load the value, just the array reference */
    if (target->kind == IDENTIFIER_EXPRESSION && !target->u.identifier.is_function)
    {
        Declaration *decl = target->u.identifier.u.declaration;
        if (decl && decl->needs_heap_lift)
        {
            cg->ctx.addr_target = target;
        }
    }
}

void leave_addrexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    cg->ctx.addr_target = NULL;

    Expression *target = expr->u.address_expression;
    if (!target || !target->type)
    {
        fprintf(stderr, "address target has no type\n");
        exit(1);
    }

    /* Check for function reference (function pointers not supported) */
    if (target->kind == IDENTIFIER_EXPRESSION && target->u.identifier.is_function)
    {
        const char *func_name = target->u.identifier.name;
        const char *path = expr->input_location.path;
        int line = expr->line_number;
        fprintf(stderr, "%s:%d: Function pointer not supported: &%s\n",
                path ? path : "<unknown>", line, func_name ? func_name : "?");
        fprintf(stderr, "       Cminor does not support function pointers.\n");
        exit(1);
    }

    if (target->kind == IDENTIFIER_EXPRESSION && !target->u.identifier.is_function)
    {
        Declaration *decl = target->u.identifier.u.declaration;
        if (decl && decl->needs_heap_lift)
        {
            /* Stack: [array_ref] from leave_identifierexpr */
            /* Create pointer with offset 0 */
            codebuilder_build_iconst(cg->builder, 0);
            /* Stack: [array_ref, 0] */
            cg_emit_ptr_create(cg, expr->type);
            handle_for_expression_leave(cg, expr);
            return;
        }

        /* ERROR: Non-heap-lifted variable address creates copy, not reference.
         * Writes through this pointer will NOT update the original. */
        const char *var_name = target->u.identifier.name;
        const char *path = expr->input_location.path;
        int line = expr->line_number;
        const char *reason = "non-heap-lifted";
        if (decl->is_static)
        {
            reason = "static variable";
        }
        else if (decl->class_name)
        {
            reason = "global variable";
        }
        fprintf(stderr, "%s:%d: Address of %s (&%s) not supported\n",
                path ? path : "<unknown>", line, reason, var_name ? var_name : "?");
        fprintf(stderr, "       Workaround: Use a local variable with heap-lift.\n");
        exit(1);
    }
    else if (target->kind == ARRAY_EXPRESSION)
    {
        /* Check if this is a pointer subscript or array subscript */
        Expression *array_base = target->u.array_expression.array;
        if (array_base && cs_type_is_pointer(array_base->type))
        {
            /* Struct pointer subscript: &ptr[i]
             * Stack: [__objectPtr, index]
             * Use ptr_add to create new ptr with offset adjusted */
            cg_emit_ptr_add(cg, array_base->type);
            handle_for_expression_leave(cg, expr);
            return;
        }

        /* Regular array subscript: &arr[i]
         * Stack: [array_ref, index] from leave_arrayexpr (when addr_target is set) */
        cg_emit_ptr_create(cg, expr->type);
        handle_for_expression_leave(cg, expr);
        return;
    }
    else if (target->kind == MEMBER_EXPRESSION)
    {
        /* ERROR: Taking address of struct member creates a COPY, not a reference.
         * Writes through this pointer will NOT update the original field.
         * Use local variables instead: char *x = NULL; func(&x); obj->field = x; */
        const char *field_name = target->u.member_expression.member_name;
        const char *path = expr->input_location.path;
        int line = expr->line_number;
        fprintf(stderr, "%s:%d: Address of struct member (&...->%s) not supported\n",
                path ? path : "<unknown>", line, field_name ? field_name : "?");
        fprintf(stderr, "       Workaround: Use a local variable.\n");
        fprintf(stderr, "       Example: int x = obj->%s; func(&x); obj->%s = x;\n",
                field_name ? field_name : "field", field_name ? field_name : "field");
        exit(1);
    }

    fprintf(stderr, "address-of operator not supported for this expression kind: %d\n", target->kind);
    exit(1);
}

/* Handler for DEREFERENCE_EXPRESSION (*ptr)
 * Calls __ptr_deref or __ptr_store for pointer access. */
void leave_derefexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    Expression *target = expr->u.dereference_expression;

    if (!target || !target->type)
    {
        fprintf(stderr, "dereference target has no type\n");
        exit(1);
    }

    /* Check if this is an assignment target */
    bool is_assign_target = (cg->ctx.assign_target == expr);
    /* Check if this is an increment/decrement target */
    bool is_inc_target = (cg->ctx.inc_target == expr);

    if (is_assign_target || is_inc_target)
    {
        /* For assignment or increment, leave pointer on stack */
        /* Stack: [ptr] - ready for __ptr_store with value */
        handle_for_expression_leave(cg, expr);
        return;
    }

    /* For reading, call __ptr_deref with type suffix */
    /* Stack: [ptr] */
    cg_emit_ptr_deref(cg, target->type);
    /* Stack: [value] */

    /* For object pointer dereference, add checkcast to the specific type.
     * __ptr_deref_object returns Object, but we need the specific type. */
    TypeSpecifier *pointee = cs_type_child(target->type);
    if (pointee && cg_pointer_runtime_kind(target->type) == CG_PTR_RUNTIME_OBJECT)
    {
        /* For struct */
        if (cs_type_is_named(pointee) && cs_type_is_basic_struct_or_union(pointee))
        {
            const char *struct_name = cs_type_user_type_name(pointee);
            if (struct_name)
            {
                int class_idx = find_class_index(cg, struct_name);
                if (class_idx != -1)
                {
                    int struct_class_idx = cg_find_or_add_class(cg, struct_name, class_idx);
                    codebuilder_build_checkcast(cg->builder, struct_class_idx);
                }
            }
        }
        /* For pointer (struct **, int **, etc.) */
        else if (cs_type_is_pointer(pointee))
        {
            cg_emit_checkcast_for_pointer_type(cg, pointee);
        }
    }

    handle_for_expression_leave(cg, expr);
}
