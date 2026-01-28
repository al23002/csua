#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codegen_constants.h"
#include "codegen_jvm_types.h"
#include "codegen_symbols.h"
#include "codegenvisitor.h"
#include "codegenvisitor_expr_values.h"
#include "codebuilder_ptr.h"
#include "codegenvisitor_util.h"
#include "codegenvisitor_expr_util.h"
#include "codebuilder_part1.h"
#include "codebuilder_part2.h"
#include "codebuilder_part3.h"
#include "codebuilder_label.h"
#include "cminor_type.h"
#include "synthetic_codegen.h"

void enter_noop_expr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    mark_for_condition_start(cg, expr);
}

void leave_noop_expr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    handle_for_expression_leave(cg, expr);
}

void enter_intexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    mark_for_condition_start(cg, expr);
}

void leave_intexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    codebuilder_build_iconst(cg->builder, expr->u.int_value);
    handle_for_expression_leave(cg, expr);
}

void enter_longexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    mark_for_condition_start(cg, expr);
}

void leave_longexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    codebuilder_build_lconst(cg->builder, expr->u.long_value);
    handle_for_expression_leave(cg, expr);
}

void enter_floatexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    mark_for_condition_start(cg, expr);
}

void leave_floatexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    codebuilder_build_fconst(cg->builder, expr->u.float_value);
    handle_for_expression_leave(cg, expr);
}

void enter_doubleexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    mark_for_condition_start(cg, expr);
}

void leave_doubleexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    codebuilder_build_dconst(cg->builder, expr->u.double_value);
    handle_for_expression_leave(cg, expr);
}

void enter_boolexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    mark_for_condition_start(cg, expr);
}

void leave_boolexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    codebuilder_build_iconst(cg->builder, expr->u.bool_value ? 1 : 0);
    handle_for_expression_leave(cg, expr);
}

void enter_nullexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    mark_for_condition_start(cg, expr);
}

void leave_nullexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;

    /*
     * If the NULL expression has a pointer type (e.g., from ternary operator
     * or explicit context), generate a pointer wrapper object with base=null.
     * This ensures type consistency at control flow merge points.
     */
    TypeSpecifier *type = expr->type;
    if (type && cs_type_is_pointer(type))
    {
        /* Push null array (base) */
        codebuilder_build_aconst_null(cg->builder);
        /* Push offset 0 */
        codebuilder_build_iconst(cg->builder, 0);
        /* Create pointer wrapper: __XPtr with null base */
        cg_emit_ptr_create(cg, type);
    }
    else
    {
        /* Non-pointer context: just push aconst_null */
        codebuilder_build_aconst_null(cg->builder);
    }
    handle_for_expression_leave(cg, expr);
}

void enter_stringexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    mark_for_condition_start(cg, expr);
}

void leave_stringexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    CS_String str = expr->u.string_value;

    /* Get constant pool from output */
    ConstantPoolBuilder *cp = code_output_cp(cg->output);

    /* Add string with null terminator to constant pool
     * "hello" -> "hello\0" so getBytes() includes the null byte */
    char *null_term_str = (char *)calloc(str.len + 1, sizeof(char));
    memcpy(null_term_str, str.data, str.len);
    int str_idx = cp_builder_add_string_len(cp, null_term_str, str.len + 1);
    codebuilder_build_ldc(cg->builder, str_idx, CF_VAL_OBJECT);

    /* Get StandardCharsets.UTF_8 */
    int utf8_field_idx = cp_builder_add_fieldref(
        cp, "java/nio/charset/StandardCharsets", "UTF_8",
        "Ljava/nio/charset/Charset;");
    codebuilder_build_getstatic(cg->builder, utf8_field_idx);

    /* Call String.getBytes(Charset) -> byte[] (already null-terminated) */
    int getbytes_idx = cp_builder_add_methodref(
        cp, "java/lang/String", "getBytes",
        "(Ljava/nio/charset/Charset;)[B");
    codebuilder_build_invokevirtual(cg->builder, getbytes_idx);

    /* Push offset 0 */
    codebuilder_build_iconst(cg->builder, 0);

    /* Create __charPtr inline */
    cg_emit_ptr_create_by_type_index(cg, PTR_TYPE_CHAR);

    handle_for_expression_leave(cg, expr);
}

void leave_memberexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    Expression *target = expr->u.member_expression.target;
    const char *member_name = expr->u.member_expression.member_name;

    if (!target || !target->type)
    {
        fprintf(stderr, "member expression target has no type\n");
        exit(1);
    }

    TypeSpecifier *struct_type = target->type;
    bool via_pointer = expr->u.member_expression.via_pointer;

    bool is_assign_target = (cg->ctx.assign_target == expr);
    bool is_inc_target = (cg->ctx.inc_target == expr);

    /* For -> access on struct pointer (__objectPtr):
     * Stack has: [__objectPtr]
     * Need to convert to: [struct_object]
     *
     * Generate:
     *   dup                        ; [ptr, ptr]
     *   getfield __objectPtr.base  ; [ptr, Object[]]
     *   swap                       ; [Object[], ptr]
     *   getfield __objectPtr.offset; [Object[], int]
     *   aaload                     ; [Object]
     *   checkcast StructName       ; [StructName]
     */
    if (via_pointer && cs_type_is_pointer(struct_type))
    {
        struct_type = cs_type_child(struct_type);
        const char *struct_name = cs_type_user_type_name(struct_type);

        if (struct_name && cs_type_is_named(struct_type))
        {
            /* Dereference __objectPtr to get the actual struct object */
            const char *ptr_class = "__objectPtr";
            int base_field_idx = cp_builder_add_fieldref(code_output_cp(cg->output),
                                                         ptr_class, "base",
                                                         "[Ljava/lang/Object;");
            int offset_field_idx = cp_builder_add_fieldref(code_output_cp(cg->output),
                                                           ptr_class, "offset", "I");
            int struct_class_idx = cg_find_or_add_class(cg, struct_name, -1);

            codebuilder_build_dup(cg->builder);
            codebuilder_build_getfield(cg->builder, base_field_idx);
            codebuilder_build_swap(cg->builder);
            codebuilder_build_getfield(cg->builder, offset_field_idx);
            codebuilder_build_aaload(cg->builder);
            codebuilder_build_checkcast(cg->builder, struct_class_idx);
        }
    }
    else if (cs_type_is_pointer(struct_type))
    {
        struct_type = cs_type_child(struct_type);
    }

    if (!cs_type_is_named(struct_type) && !cs_type_is_basic_struct_or_union(struct_type))
    {
        fprintf(stderr, "member expression target is not a struct: member='%s', kind=%d\n",
                member_name, cs_type_kind(struct_type));
        exit(1);
    }

    const char *class_name = cg_get_struct_class_name(cg, struct_type);
    if (!class_name)
    {
        fprintf(stderr, "struct type has no name (kind=%d)\n",
                cs_type_kind(struct_type));
        exit(1);
    }

    int class_idx = find_class_index(cg, class_name);
    if (class_idx == -1)
    {
        fprintf(stderr, "struct '%s' not found in class definitions\n", class_name);
        exit(1);
    }

    /* Check for special union handling */
    CS_UnionKind union_kind = cs_union_kind(struct_type);
    StructMember *member = cs_lookup_struct_member(cg->compiler, struct_type, member_name);
    TypeSpecifier *field_type = member ? member->type : NULL;

    /* Determine physical field name and whether conversion is needed */
    const char *physical_field_name = member_name;
    bool need_float_conversion = false;
    bool need_double_conversion = false;
    bool need_checkcast = false;
    bool need_unbox = false;
    bool need_auto_create_struct = false;
    const char *checkcast_class = NULL;

    switch (union_kind)
    {
    case CS_UNION_KIND_TYPE_PUNNING_INT_FLOAT:
        physical_field_name = "_bits";
        /* If accessing float member, need to convert from int bits */
        if (field_type && cs_type_is_float_exact(field_type))
        {
            need_float_conversion = true;
        }
        break;

    case CS_UNION_KIND_TYPE_PUNNING_LONG_DOUBLE:
        physical_field_name = "_bits";
        /* If accessing double member, need to convert from long bits */
        if (field_type && cs_type_is_double_exact(field_type))
        {
            need_double_conversion = true;
        }
        break;

    case CS_UNION_KIND_REFERENCE:
        physical_field_name = "_ref";
        /* Need checkcast/unbox based on field type */
        if (field_type && cs_type_is_pointer(field_type))
        {
            /* Pointer type: checkcast to pointer class (e.g., __intPtr, __charPtr) */
            PtrTypeIndex ptr_idx = (PtrTypeIndex)cg_pointer_runtime_kind(field_type);
            checkcast_class = ptr_type_class_name(ptr_idx);
            need_checkcast = (checkcast_class != NULL);
        }
        else if (field_type && cs_type_is_aggregate(field_type))
        {
            /* Struct/union value type: checkcast to struct class
             * Also need to auto-create if null (for write access) */
            const char *struct_class = cg_get_struct_class_name(cg, field_type);
            if (struct_class)
            {
                need_checkcast = true;
                need_auto_create_struct = true;
                checkcast_class = struct_class;
            }
        }
        else if (field_type && (cs_type_is_primitive(field_type) || cs_type_is_enum(field_type)))
        {
            /* Primitive or enum type: unbox from boxed Object */
            need_unbox = true;
        }
        break;

    default:
        /* NOT_UNION or UNSUPPORTED: use original field name */
        break;
    }

    int field_idx = find_field_index(cg, class_idx, physical_field_name);
    if (field_idx == -1)
    {
        fprintf(stderr, "field '%s' not found in struct '%s'\n", physical_field_name, class_name);
        exit(1);
    }

    /* For special unions, we need different type descriptors */
    TypeSpecifier *storage_type = field_type;
    if (union_kind == CS_UNION_KIND_TYPE_PUNNING_INT_FLOAT ||
        union_kind == CS_UNION_KIND_TYPE_PUNNING_LONG_DOUBLE ||
        union_kind == CS_UNION_KIND_REFERENCE)
    {
        storage_type = NULL; /* Use descriptor from class definition */
    }

    int const_idx = cg_find_or_add_struct_field(cg, class_name, physical_field_name,
                                                field_idx, storage_type);

    if (is_inc_target)
    {
        /* For increment target, leave struct object on stack */
        /* The actual getfield/putfield will be handled in leave_incexpr */
    }
    else if (!is_assign_target)
    {
        /* For struct union members, we need to auto-create if null.
         * Save the parent union object before getfield so we can store back if needed */
        int union_temp_local = -1;
        if (need_auto_create_struct && checkcast_class)
        {
            union_temp_local = allocate_temp_local(cg);
            codebuilder_build_dup(cg->builder);
            codebuilder_build_astore(cg->builder, union_temp_local);
        }

        codebuilder_build_getfield(cg->builder, const_idx);

        /* For struct union members, check if null and create if needed */
        if (need_auto_create_struct && checkcast_class && union_temp_local >= 0)
        {
            /* Stack: [_ref] (which may be null or wrong type)
             * Generate: if null/wrong type, create new struct and store it */
            int target_class_idx = cp_builder_add_class(code_output_cp(cg->output), checkcast_class);
            int init_method_idx = cp_builder_add_methodref(code_output_cp(cg->output),
                                                           checkcast_class, "<init>", "()V");

            /* dup; instanceof; ifne HAVE_IT (jump if != 0, i.e., correct type) */
            codebuilder_build_dup(cg->builder);
            codebuilder_build_instanceof(cg->builder, target_class_idx);
            CB_Label *have_it = codebuilder_create_label(cg->builder);
            CB_Label *done_label = codebuilder_create_label(cg->builder);
            codebuilder_jump_if(cg->builder, have_it);

            /* Create new instance: pop; aload temp; new; dup_x1; dup; invokespecial; putfield; goto done */
            codebuilder_build_pop(cg->builder);
            codebuilder_build_aload(cg->builder, union_temp_local);
            codebuilder_build_new(cg->builder, target_class_idx);
            codebuilder_build_dup_x1(cg->builder);
            codebuilder_build_dup(cg->builder);
            codebuilder_build_invokespecial(cg->builder, init_method_idx);
            codebuilder_build_putfield(cg->builder, const_idx);
            codebuilder_jump(cg->builder, done_label);

            /* HAVE_IT: checkcast */
            codebuilder_place_label(cg->builder, have_it);
            codebuilder_build_checkcast(cg->builder, target_class_idx);

            /* DONE: */
            codebuilder_place_label(cg->builder, done_label);

            /* Skip the normal checkcast handling since we already did it */
            need_checkcast = false;
        }

        /* Apply type conversions for special unions */
        if (need_float_conversion)
        {
            /* int -> float via Float.intBitsToFloat */
            int method_idx = cp_builder_add_methodref(code_output_cp(cg->output),
                                                      "java/lang/Float", "intBitsToFloat",
                                                      "(I)F");
            codebuilder_build_invokestatic(cg->builder, method_idx);
        }
        else if (need_double_conversion)
        {
            /* long -> double via Double.longBitsToDouble */
            int method_idx = cp_builder_add_methodref(code_output_cp(cg->output),
                                                      "java/lang/Double", "longBitsToDouble",
                                                      "(J)D");
            codebuilder_build_invokestatic(cg->builder, method_idx);
        }
        else if (need_unbox && field_type)
        {
            /* Unbox from Object - for REFERENCE union with primitive types */
            /* Stack: Object -> unbox to primitive */

            if (cs_type_is_int_exact(field_type) ||
                cs_type_is_char_exact(field_type) ||
                cs_type_is_short_exact(field_type) ||
                cs_type_is_bool(field_type) ||
                cs_type_is_enum(field_type))
            {
                /* checkcast Integer; invokevirtual intValue */
                int class_idx = cp_builder_add_class(code_output_cp(cg->output), "java/lang/Integer");
                codebuilder_build_checkcast(cg->builder, class_idx);
                int method_idx = cp_builder_add_methodref(code_output_cp(cg->output),
                                                          "java/lang/Integer", "intValue", "()I");
                codebuilder_build_invokevirtual(cg->builder, method_idx);
            }
            else if (cs_type_is_long_exact(field_type))
            {
                int class_idx = cp_builder_add_class(code_output_cp(cg->output), "java/lang/Long");
                codebuilder_build_checkcast(cg->builder, class_idx);
                int method_idx = cp_builder_add_methodref(code_output_cp(cg->output),
                                                          "java/lang/Long", "longValue", "()J");
                codebuilder_build_invokevirtual(cg->builder, method_idx);
            }
            else if (cs_type_is_double_exact(field_type))
            {
                int class_idx = cp_builder_add_class(code_output_cp(cg->output), "java/lang/Double");
                codebuilder_build_checkcast(cg->builder, class_idx);
                int method_idx = cp_builder_add_methodref(code_output_cp(cg->output),
                                                          "java/lang/Double", "doubleValue", "()D");
                codebuilder_build_invokevirtual(cg->builder, method_idx);
            }
            else if (cs_type_is_float_exact(field_type))
            {
                int class_idx = cp_builder_add_class(code_output_cp(cg->output), "java/lang/Float");
                codebuilder_build_checkcast(cg->builder, class_idx);
                int method_idx = cp_builder_add_methodref(code_output_cp(cg->output),
                                                          "java/lang/Float", "floatValue", "()F");
                codebuilder_build_invokevirtual(cg->builder, method_idx);
            }
        }
        else if (need_checkcast && checkcast_class)
        {
            /* Checkcast based on field type */
            if (field_type && cs_type_is_pointer(field_type))
            {
                /* Pointer type: checkcast to pointer class directly (checkcast_class is already __intPtr, etc.) */
                int target_class_idx = cp_builder_add_class(code_output_cp(cg->output), checkcast_class);
                codebuilder_build_checkcast(cg->builder, target_class_idx);
            }
            else if (field_type && cs_type_is_aggregate(field_type))
            {
                /* Struct/union value type: checkcast to class directly */
                int target_class_idx = cp_builder_add_class(code_output_cp(cg->output), checkcast_class);
                codebuilder_build_checkcast(cg->builder, target_class_idx);
            }
        }

        /* Zero-extend unsigned char/short fields after getfield.
         * JVM's getfield for byte/short fields sign-extends, so we need to mask.
         * Skip if union conversion already handled the value (need_unbox, need_float/double_conversion). */
        if (field_type && !need_float_conversion && !need_double_conversion && !need_unbox &&
            union_kind == CS_UNION_KIND_NOT_UNION)
        {
            if (cs_type_is_char_exact(field_type) && cs_type_is_unsigned(field_type))
            {
                codebuilder_build_iconst(cg->builder, 255);
                codebuilder_build_iand(cg->builder);
            }
            else if (cs_type_is_short_exact(field_type) && cs_type_is_unsigned(field_type))
            {
                codebuilder_build_iconst(cg->builder, 65535);
                codebuilder_build_iand(cg->builder);
            }
        }
    }
    else
    {
        /* For assignment target, store union_kind info for later use in assignment codegen */
        /* The actual putfield with conversion will be handled in leave_assign */
    }

    handle_for_expression_leave(cg, expr);
}

void enter_identifierexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    mark_for_condition_start(cg, expr);
}

void leave_identifierexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;

    if (expr->u.identifier.is_function)
    {
        handle_for_expression_leave(cg, expr);
        return;
    }

    /* Handle enum member: push value as int constant */
    if (expr->u.identifier.is_enum_member && expr->u.identifier.u.enum_member)
    {
        EnumMember *member = expr->u.identifier.u.enum_member;
        codebuilder_build_iconst(cg->builder, member->value);
        handle_for_expression_leave(cg, expr);
        return;
    }

    Declaration *decl = expr->u.identifier.u.declaration;
    if (!decl)
    {
        fprintf(stderr, "identifier declaration missing in codegen\n");
        exit(1);
    }

    CodegenSymbolInfo sym = cg_ensure_symbol(cg, decl);
    CF_ValueTag tag = cg_decl_value_tag(decl);

    if (cg->ctx.inc_target == expr)
    {
        /* Increment/decrement: skip loading value here */
        return;
    }

    if (cg->ctx.assign_target == expr)
    {
        /* Assignment target: if heap-lifted, load array ref instead of value */
        if (decl->needs_heap_lift)
        {
            if (sym.kind == CG_SYMBOL_STATIC)
            {
                int pool_idx = cg_find_or_add_field(cg, decl);
                codebuilder_build_getstatic(cg->builder, pool_idx);
            }
            else
            {
                codebuilder_build_aload(cg->builder, sym.index);
            }
        }
        return;
    }

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
        case CF_VAL_OBJECT:
        case CF_VAL_NULL:
            codebuilder_build_aload(cg->builder, sym.index);
            break;
        default:
            fprintf(stderr, "leave_identifierexpr: invalid tag %d for local %d\n", tag, sym.index);
            exit(1);
        }
    }

    /* Handle heap-lifted variables (boxed in array): load element value */
    /* But skip if this is the target of an address-of expression (&x) */
    bool is_addr_target = (cg->ctx.addr_target == expr);
    if (decl->needs_heap_lift && !is_addr_target)
    {
        /* Stack has [array_ref], load element 0 */
        codebuilder_build_iconst(cg->builder, 0);
        if (cs_type_is_pointer(decl->type) || cs_type_is_array(decl->type) ||
            cs_type_is_basic_struct_or_union(decl->type))
        {
            codebuilder_build_aaload(cg->builder);
            /* Add checkcast to cast from Object to actual type */
            if (cs_type_is_pointer(decl->type))
            {
                cg_emit_checkcast_for_pointer_type(cg, decl->type);
            }
            else if (cs_type_is_array(decl->type))
            {
                /* Cast Object to the actual array type (e.g., [I for int[]) */
                const char *class_name = cg_jvm_class_name(decl->type);
                int class_idx = cp_builder_add_class(code_output_cp(cg->output), class_name);
                codebuilder_build_checkcast(cg->builder, class_idx);
            }
            else if (cs_type_is_basic_struct_or_union(decl->type))
            {
                const char *struct_name = cs_type_user_type_name(decl->type);
                if (struct_name)
                {
                    int struct_class_idx = cg_find_or_add_class(cg, struct_name, -1);
                    codebuilder_build_checkcast(cg->builder, struct_class_idx);
                }
            }
        }
        else if (cs_type_is_double_exact(decl->type))
        {
            codebuilder_build_daload(cg->builder);
        }
        else
        {
            if (cs_type_is_char_exact(decl->type) || cs_type_is_bool(decl->type))
            {
                codebuilder_build_baload(cg->builder);
            }
            else if (cs_type_is_short_exact(decl->type))
            {
                codebuilder_build_saload(cg->builder);
            }
            else if (cs_type_is_long_exact(decl->type))
            {
                codebuilder_build_laload(cg->builder);
            }
            else if (cs_type_is_float_exact(decl->type))
            {
                codebuilder_build_faload(cg->builder);
            }
            else if (cs_type_is_int_exact(decl->type))
            {
                codebuilder_build_iaload(cg->builder);
            }
            else
            {
                /* Named types (struct, typedef like void*) use aaload */
                codebuilder_build_aaload(cg->builder);
            }
        }
    }

    handle_for_expression_leave(cg, expr);
}

void leave_arrayexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    Expression *array = expr->u.array_expression.array;
    if (!array || !array->type)
    {
        fprintf(stderr, "array expression is missing target type\n");
        exit(1);
    }

    bool is_assign_target = (cg->ctx.assign_target == expr);
    bool is_addr_target = (cg->ctx.addr_target == expr);

    /* Check for pointer subscript access: ptr[i] where ptr is T* */
    if (cs_type_is_pointer(array->type))
    {
        TypeSpecifier *pointee = cs_type_child(array->type);
        /* For struct pointer subscript access.
         * Skip typedef aliases for primitive types (e.g., uint32_t*). */
        if (pointee && cs_type_is_named(pointee) && cs_type_is_basic_struct_or_union(pointee))
        {
            /* Struct pointer subscript access via __objectPtr
             * Stack has: [__objectPtr, index]
             * For address-of: leave stack as-is for leave_addrexpr
             * Otherwise: inline ptr_subscript to get base[offset+index]
             * Then checkcast to struct type
             */
            if (is_addr_target || is_assign_target)
            {
                /* Leave [__objectPtr, index] on stack for leave_addrexpr or leave_assignexpr */
                handle_for_expression_leave(cg, expr);
                return;
            }

            const char *struct_name = cs_type_user_type_name(pointee);
            if (struct_name)
            {
                int struct_class_idx = cg_find_or_add_class(cg, struct_name, -1);

                /* Call __ptr_subscript_object(__objectPtr, index) -> Object */
                cg_emit_ptr_subscript(cg, array->type);
                /* Checkcast Object -> StructName */
                codebuilder_build_checkcast(cg->builder, struct_class_idx);

                handle_for_expression_leave(cg, expr);
                return;
            }
        }

        /* Pointer-to-pointer subscript: T** where T is a pointer type
         * After __ptr_subscript_object, result is Object, need checkcast to __XPtr */
        if (pointee && cs_type_is_pointer(pointee))
        {
            if (is_addr_target || is_assign_target)
            {
                /* Leave [__objectPtr, index] on stack */
                handle_for_expression_leave(cg, expr);
                return;
            }

            /* Call __ptr_subscript_object(__objectPtr, index) -> Object */
            cg_emit_ptr_subscript(cg, array->type);

            /* Checkcast Object -> appropriate pointer type */
            cg_emit_checkcast_for_pointer_type(cg, pointee);

            handle_for_expression_leave(cg, expr);
            return;
        }

        /* Primitive pointer subscript access via __XPtr
         * Stack has: [__XPtr, index]
         * For assignment target: leave stack as-is for later store_subscript call
         * For read: inline ptr_subscript to get base[offset+index]
         */
        if (!is_assign_target && !is_addr_target)
        {
            cg_emit_ptr_subscript(cg, array->type);
        }
        /* For assignment/address, leave [ptr, index] on stack */
        handle_for_expression_leave(cg, expr);
        return;
    }

    if (!cs_type_is_array(array->type))
    {
        fprintf(stderr, "array expression target is not an array\n");
        exit(1);
    }

    TypeSpecifier *element_type = cs_type_child(array->type);
    bool element_is_array = element_type && cs_type_is_array(element_type);

    /* Array-of-arrays access:
     * - If element is an array (e.g., matrix[i] where matrix is int[][]),
     *   use AALOAD to get the inner array reference.
     * - If element is a primitive (e.g., matrix[i][j] final access),
     *   use IALOAD/DALOAD to get the value.
     * Stack before: [array_ref, index]
     * Stack after: [element] (either array ref or value) */

    if (!is_assign_target && !is_addr_target)
    {
        if (element_is_array)
        {
            /* Accessing outer dimension: get inner array reference */
            codebuilder_build_aaload(cg->builder);
        }
        else
        {
            /* Accessing final dimension: get the value */
            if (element_type && (cs_type_is_array(element_type) ||
                                 cs_type_is_pointer(element_type)))
            {
                codebuilder_build_aaload(cg->builder);
            }
            else if (element_type && cs_type_is_double_exact(element_type))
            {
                codebuilder_build_daload(cg->builder);
            }
            else if (element_type)
            {
                if (cs_type_is_char_exact(element_type) || cs_type_is_bool(element_type))
                {
                    codebuilder_build_baload(cg->builder);
                    /* For unsigned char (uint8_t), mask with 0xFF */
                    if (cs_type_is_unsigned(element_type))
                    {
                        codebuilder_build_iconst(cg->builder, 255);
                        codebuilder_build_iand(cg->builder);
                    }
                }
                else if (cs_type_is_short_exact(element_type))
                {
                    codebuilder_build_saload(cg->builder);
                }
                else if (cs_type_is_long_exact(element_type))
                {
                    codebuilder_build_laload(cg->builder);
                }
                else if (cs_type_is_float_exact(element_type))
                {
                    codebuilder_build_faload(cg->builder);
                }
                else if (cs_type_is_int_exact(element_type))
                {
                    codebuilder_build_iaload(cg->builder);
                }
                else
                {
                    /* Named types (struct, typedef like void*) use aaload */
                    codebuilder_build_aaload(cg->builder);
                }
            }
            else
            {
                codebuilder_build_iaload(cg->builder);
            }
        }
    }
    /* For assignment target or address-of, leave [array_ref, index] on stack */

    handle_for_expression_leave(cg, expr);
}

void enter_sizeofexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;
    mark_for_condition_start(cg, expr);
}

void leave_sizeofexpr(Expression *expr, Visitor *visitor)
{
    CodegenVisitor *cg = (CodegenVisitor *)visitor;

    /* For sizeof(type), this is handled specially in calloc */
    if (expr->u.sizeof_expression.is_type)
    {
        /* No code generation - used only as calloc argument */
        handle_for_expression_leave(cg, expr);
        return;
    }

    /* For sizeof identifier, sizeof arr[i], sizeof *arr - push computed value */
    int value = expr->u.sizeof_expression.computed_value;
    codebuilder_build_iconst(cg->builder, value);
    handle_for_expression_leave(cg, expr);
}
