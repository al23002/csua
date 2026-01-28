#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codebuilder_part3.h"
#include "codebuilder_core.h"
#include "codebuilder_internal.h"
#include "codebuilder_types.h"
#include "codebuilder_label.h"
#include "classfile_opcode.h"

void codebuilder_build_if(CodeBuilder *builder, IfCond cond, int32_t offset)
{
    classfile_opcode_emit_if(builder->method, cond, offset);
    /* All if instructions pop one value from the stack */
    cb_pop(builder);
}

void codebuilder_build_if_icmp(CodeBuilder *builder, IntCmpCond cond, int32_t offset)
{
    classfile_opcode_emit_if_icmp(builder->method, cond, offset);
    /* if_icmp instructions pop two integers from the stack */
    cb_pop(builder);
    cb_pop(builder);
}

void codebuilder_build_if_acmp(CodeBuilder *builder, ACmpCond cond, int32_t offset)
{
    classfile_opcode_emit_if_acmp(builder->method, cond, offset);
    /* if_acmp instructions pop two object references from the stack */
    cb_pop(builder);
    cb_pop(builder);
}

void codebuilder_build_goto(CodeBuilder *builder, int32_t offset)
{
    classfile_opcode_emit_goto(builder->method, offset);
    /* Do NOT clear stack here. The frame state is preserved for labels
     * that may follow in dead code. The stack will be restored/merged
     * when a label with a saved frame is placed. */
}

void codebuilder_build_ireturn(CodeBuilder *builder)
{
    classfile_opcode_emit_ireturn(builder->method);
    cb_pop(builder);
    cb_set_stack_depth(builder, 0);
    codebuilder_mark_dead(builder);
}

void codebuilder_build_lreturn(CodeBuilder *builder)
{
    classfile_opcode_emit_lreturn(builder->method);
    cb_pop(builder);
    cb_set_stack_depth(builder, 0);
    codebuilder_mark_dead(builder);
}

void codebuilder_build_freturn(CodeBuilder *builder)
{
    classfile_opcode_emit_freturn(builder->method);
    cb_pop(builder);
    cb_set_stack_depth(builder, 0);
    codebuilder_mark_dead(builder);
}

void codebuilder_build_dreturn(CodeBuilder *builder)
{
    classfile_opcode_emit_dreturn(builder->method);
    cb_pop(builder);
    cb_set_stack_depth(builder, 0);
    codebuilder_mark_dead(builder);
}

void codebuilder_build_areturn(CodeBuilder *builder)
{
    /* Check that we're returning a reference type */
    if (builder && builder->frame && builder->frame->stack_count > 0)
    {
        CB_VerificationType stack_top = builder->frame->stack[builder->frame->stack_count - 1];
        if (stack_top.tag != CF_VERIFICATION_OBJECT &&
            stack_top.tag != CF_VERIFICATION_NULL &&
            stack_top.tag != CF_VERIFICATION_TOP &&
            stack_top.tag != CF_VERIFICATION_UNINITIALIZED &&
            stack_top.tag != CF_VERIFICATION_UNINITIALIZED_THIS)
        {
            fprintf(stderr, "WARNING: areturn with non-reference type on stack: %s (tag=%d) at pc=%d in %s\n",
                    cb_type_name(&stack_top), stack_top.tag,
                    codebuilder_current_pc(builder),
                    builder->method_name ? builder->method_name : "<unknown>");
            fprintf(stderr, "  This will cause VerifyError: 'Type %s is not assignable to reference type'\n",
                    cb_type_name(&stack_top));
        }
    }

    classfile_opcode_emit_areturn(builder->method);
    cb_pop(builder);
    cb_set_stack_depth(builder, 0);
    codebuilder_mark_dead(builder);
}

void codebuilder_build_return(CodeBuilder *builder)
{
    classfile_opcode_emit_return(builder->method);
    cb_set_stack_depth(builder, 0);
    codebuilder_mark_dead(builder);
}

void codebuilder_build_getstatic(CodeBuilder *builder, int index)
{
    classfile_opcode_emit_getstatic(builder->method, index);

    ConstantPoolBuilder *cp = builder->cp;
    if (index < cp_builder_count(cp))
    {
        CP_Constant *c = &cp_builder_entries(cp)[index];
        const char *desc = c->u.c_field.descriptor;
        if (desc && *desc)
        {
            const char *p = desc;
            cb_push(builder, cb_descriptor_type(&p));
        }
        else
        {
            cb_push(builder, cb_type_object("Ljava/lang/Object;"));
        }
    }
    else
    {
        cb_push(builder, cb_type_object("Ljava/lang/Object;"));
    }
}

void codebuilder_build_putstatic(CodeBuilder *builder, int index)
{
    classfile_opcode_emit_putstatic(builder->method, index);
    cb_pop(builder);
}

void codebuilder_build_getfield(CodeBuilder *builder, int index)
{
    /* Check object reference type on stack before getfield */
    if (builder && builder->frame && builder->frame->stack_count > 0)
    {
        CB_VerificationType stack_top = builder->frame->stack[builder->frame->stack_count - 1];

        /* Get expected class and field name from field descriptor */
        ConstantPoolBuilder *cp = builder->cp;
        const char *owner_class = NULL;
        const char *field_name = NULL;
        if (index < cp_builder_count(cp))
        {
            CP_Constant *c = &cp_builder_entries(cp)[index];
            if (c->type == CP_CONST_FIELD)
            {
                owner_class = c->u.c_field.class_name;
                field_name = c->u.c_field.name;
            }
        }

        /* Verify stack has compatible reference type */
        if (stack_top.tag != CF_VERIFICATION_OBJECT &&
            stack_top.tag != CF_VERIFICATION_NULL &&
            stack_top.tag != CF_VERIFICATION_TOP)
        {
            fprintf(stderr, "WARNING: getfield with incompatible type on stack: %s (expected object) at pc=%d in %s\n",
                    cb_type_name(&stack_top),
                    codebuilder_current_pc(builder),
                    builder->method_name ? builder->method_name : "<unknown>");
            if (owner_class && field_name)
            {
                fprintf(stderr, "  Field: %s.%s\n", owner_class, field_name);
            }
            fprintf(stderr, "  This will cause VerifyError: 'Type %s is not assignable to expected type'\n",
                    cb_type_name(&stack_top));
        }
    }

    classfile_opcode_emit_getfield(builder->method, index);

    cb_pop(builder);
    ConstantPoolBuilder *cp = builder->cp;
    if (index < cp_builder_count(cp))
    {
        CP_Constant *c = &cp_builder_entries(cp)[index];
        const char *desc = c->u.c_field.descriptor;
        if (desc && *desc)
        {
            const char *p = desc;
            cb_push(builder, cb_descriptor_type(&p));
        }
        else
        {
            cb_push(builder, cb_type_object("Ljava/lang/Object;"));
        }
    }
    else
    {
        cb_push(builder, cb_type_object("Ljava/lang/Object;"));
    }
}

void codebuilder_build_putfield(CodeBuilder *builder, int index)
{
    /* Check field value and object reference types on stack before putfield */
    if (builder && builder->frame && builder->frame->stack_count >= 2)
    {
        /* Get field info from constant pool first to determine value size */
        ConstantPoolBuilder *cp = builder->cp;
        const char *owner_class = NULL;
        const char *field_name = NULL;
        const char *field_desc = NULL;
        CB_VerificationType expected_type = cb_type_top();

        if (index < cp_builder_count(cp))
        {
            CP_Constant *c = &cp_builder_entries(cp)[index];
            if (c->type == CP_CONST_FIELD)
            {
                owner_class = c->u.c_field.class_name;
                field_name = c->u.c_field.name;
                field_desc = c->u.c_field.descriptor;

                if (field_desc)
                {
                    const char *p = field_desc;
                    expected_type = cb_descriptor_type(&p);
                }
            }
        }

        /* Calculate correct stack positions considering 2-slot types */
        int value_slots = cb_type_slots(&expected_type);
        if (value_slots == 0)
            value_slots = 1; /* default to 1 slot if unknown */

        /* For 2-slot value: stack is [..., obj, value, TOP] */
        /* For 1-slot value: stack is [..., obj, value] */
        int value_idx = builder->frame->stack_count - value_slots;
        int obj_idx = value_idx - 1;

        CB_VerificationType value_type = (value_idx >= 0 && value_idx < builder->frame->stack_count)
                                             ? builder->frame->stack[value_idx]
                                             : cb_type_top();
        CB_VerificationType obj_type = (obj_idx >= 0)
                                           ? builder->frame->stack[obj_idx]
                                           : cb_type_top();

        /* Check object reference compatibility */
        if (obj_type.tag != CF_VERIFICATION_OBJECT &&
            obj_type.tag != CF_VERIFICATION_NULL &&
            obj_type.tag != CF_VERIFICATION_TOP)
        {
            fprintf(stderr, "WARNING: putfield with non-object reference on stack at pc=%d in %s\n",
                    codebuilder_current_pc(builder),
                    builder->method_name ? builder->method_name : "<unknown>");
            if (owner_class && field_name)
            {
                fprintf(stderr, "  Field: %s.%s\n", owner_class, field_name);
            }
        }

        /* Check value type compatibility */
        if (expected_type.tag != CF_VERIFICATION_TOP && !cb_type_assignable(value_type, expected_type))
        {
            fprintf(stderr, "WARNING: putfield type mismatch at pc=%d in %s\n",
                    codebuilder_current_pc(builder),
                    builder->method_name ? builder->method_name : "<unknown>");
            if (owner_class && field_name && field_desc)
            {
                fprintf(stderr, "  Field: %s.%s %s\n", owner_class, field_name, field_desc);
            }
            fprintf(stderr, "  Stack type: %s (tag=%d)\n", cb_type_name(&value_type), value_type.tag);
            fprintf(stderr, "  Expected: %s (tag=%d)\n", cb_type_name(&expected_type), expected_type.tag);
            fprintf(stderr, "  This will cause VerifyError: 'Type %s is not assignable to %s'\n",
                    cb_type_name(&value_type), cb_type_name(&expected_type));
        }
    }

    classfile_opcode_emit_putfield(builder->method, index);
    cb_pop(builder);
    cb_pop(builder);
}

static const char *cb_get_method_descriptor(CodeBuilder *builder, int index)
{
    ConstantPoolBuilder *cp = builder->cp;
    if (index < cp_builder_count(cp))
    {
        CP_Constant *c = &cp_builder_entries(cp)[index];
        if (c->type == CP_CONST_METHOD)
        {
            return c->u.c_method.descriptor;
        }
    }
    return NULL;
}

static const char *cb_get_method_name(CodeBuilder *builder, int index)
{
    ConstantPoolBuilder *cp = builder->cp;
    if (index < cp_builder_count(cp))
    {
        CP_Constant *c = &cp_builder_entries(cp)[index];
        if (c->type == CP_CONST_METHOD)
        {
            return c->u.c_method.name;
        }
    }
    return NULL;
}

static const char *cb_get_method_class(CodeBuilder *builder, int index)
{
    ConstantPoolBuilder *cp = builder->cp;
    if (index < cp_builder_count(cp))
    {
        CP_Constant *c = &cp_builder_entries(cp)[index];
        if (c->type == CP_CONST_METHOD)
        {
            return c->u.c_method.class_name;
        }
    }
    return NULL;
}

static FunctionDeclaration *cb_get_method_decl(CodeBuilder *builder, int index)
{
    ConstantPoolBuilder *cp = builder->cp;
    if (index < cp_builder_count(cp))
    {
        CP_Constant *c = &cp_builder_entries(cp)[index];
        if (c->type == CP_CONST_METHOD)
        {
            return c->u.c_method.func_decl;
        }
    }
    return NULL;
}

void codebuilder_build_invokevirtual(CodeBuilder *builder, int index)
{
    classfile_opcode_emit_invokevirtual(builder->method, index);
    const char *descriptor = cb_get_method_descriptor(builder, index);
    FunctionDeclaration *func = cb_get_method_decl(builder, index);
    if (func)
    {
        codebuilder_apply_invoke_signature(builder, func, true);
    }
    else if (descriptor)
    {
        codebuilder_apply_invoke_descriptor(builder, descriptor, true);
    }
}

void codebuilder_build_invokespecial(CodeBuilder *builder, int index)
{
    classfile_opcode_emit_invokespecial(builder->method, index);

    const char *method_name = cb_get_method_name(builder, index);
    const char *class_name = cb_get_method_class(builder, index);
    const char *descriptor = cb_get_method_descriptor(builder, index);
    FunctionDeclaration *func = cb_get_method_decl(builder, index);

    /* Check if this is an <init> call that needs uninitialized type replacement */
    int uninit_offset = 0xFFFF;
    bool is_init_call = method_name && strcmp(method_name, "<init>") == 0;

    if (is_init_call && builder->frame->stack_count > 0)
    {
        /* Count arguments to find the receiver position */
        int arg_count = 0;
        if (descriptor)
        {
            const char *p = strchr(descriptor, '(');
            if (p)
            {
                p++;
                while (*p && *p != ')')
                {
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
                    else if (*p == 'J' || *p == 'D')
                    {
                        /* Long/double take 2 slots */
                        arg_count++;
                        p++;
                    }
                    else if (*p)
                    {
                        p++;
                    }
                    arg_count++;
                }
            }
        }

        /* Find the receiver (objectref) which is below the arguments */
        int receiver_idx = builder->frame->stack_count - arg_count - 1;
        if (receiver_idx >= 0 && receiver_idx < builder->frame->stack_count)
        {
            CB_VerificationType *receiver = &builder->frame->stack[receiver_idx];
            if (receiver->tag == CF_VERIFICATION_UNINITIALIZED)
            {
                uninit_offset = receiver->u.offset;
            }
        }
    }

    if (func)
    {
        codebuilder_apply_invoke_signature(builder, func, true);
    }
    else if (descriptor)
    {
        codebuilder_apply_invoke_descriptor(builder, descriptor, true);
    }

    /* After <init>, replace all uninitialized[offset] with the class type */
    if (is_init_call && uninit_offset != 0xFFFF && class_name)
    {
        /* Build class descriptor: "L" + class_name + ";" */
        int len = strlen(class_name);
        char *class_desc = (char *)calloc(len + 3, sizeof(char));
        class_desc[0] = 'L';
        strncpy(class_desc + 1, class_name, len);
        class_desc[len + 1] = ';';
        class_desc[len + 2] = '\0';

        CB_VerificationType initialized_type = cb_type_object(class_desc);

        /* Replace in stack */
        for (uint16_t i = 0; i < builder->frame->stack_count; i++)
        {
            if (builder->frame->stack[i].tag == CF_VERIFICATION_UNINITIALIZED &&
                builder->frame->stack[i].u.offset == uninit_offset)
            {
                builder->frame->stack[i] = initialized_type;
            }
        }

        /* Replace in locals */
        for (uint16_t i = 0; i < builder->frame->locals_count; i++)
        {
            if (builder->frame->locals[i].tag == CF_VERIFICATION_UNINITIALIZED &&
                builder->frame->locals[i].u.offset == uninit_offset)
            {
                builder->frame->locals[i] = initialized_type;
            }
        }
    }
}

void codebuilder_build_invokestatic(CodeBuilder *builder, int index)
{
    classfile_opcode_emit_invokestatic(builder->method, index);
    const char *descriptor = cb_get_method_descriptor(builder, index);
    FunctionDeclaration *func = cb_get_method_decl(builder, index);
    if (func)
    {
        codebuilder_apply_invoke_signature(builder, func, false);
    }
    else if (descriptor)
    {
        codebuilder_apply_invoke_descriptor(builder, descriptor, false);
    }
}

void codebuilder_build_new(CodeBuilder *builder, int class_index)
{
    int pc = method_code_size(builder->method);
    classfile_opcode_emit_new(builder->method, class_index);
    cb_push(builder, cb_type_uninitialized((uint16_t)pc));
}

void codebuilder_build_newarray(CodeBuilder *builder, int atype)
{
    classfile_opcode_emit_newarray(builder->method, atype);
    cb_pop(builder);

    const char *descriptor = "[I";
    switch (atype)
    {
    case 4:
        descriptor = "[Z";
        break;
    case 5:
        descriptor = "[C";
        break;
    case 6:
        descriptor = "[F";
        break;
    case 7:
        descriptor = "[D";
        break;
    case 8:
        descriptor = "[B";
        break;
    case 9:
        descriptor = "[S";
        break;
    case 10:
        descriptor = "[I";
        break;
    case 11:
        descriptor = "[J";
        break;
    default:
        break;
    }

    cb_push(builder, cb_type_object(descriptor));
}

void codebuilder_build_anewarray(CodeBuilder *builder, int class_index)
{
    classfile_opcode_emit_anewarray(builder->method, class_index);
    cb_pop(builder);

    /* Look up class name from constant pool to build array type descriptor */
    ConstantPoolBuilder *cp = builder->cp;
    if (class_index < cp_builder_count(cp))
    {
        CP_Constant *c = &cp_builder_entries(cp)[class_index];
        if (c->type == CP_CONST_CLASS && c->u.c_class.name)
        {
            /* c_class.name contains internal class name format:
             * - For classes: "java/lang/String" (no L and ;)
             * - For arrays: "[I", "[Ljava/lang/Object;" (descriptor format) */
            const char *name = c->u.c_class.name;
            int len = strlen(name);
            char *array_desc;

            if (name[0] == '[')
            {
                /* Element is already an array type (e.g., "[I" for int[]).
                 * Result is just "[" + name (e.g., "[[I" for int[][]) */
                array_desc = (char *)calloc(len + 2, sizeof(char));
                array_desc[0] = '[';
                strcpy(array_desc + 1, name);
            }
            else
            {
                /* Element is a class (e.g., "java/lang/String").
                 * Build array descriptor: "[L" + internal_name + ";" */
                array_desc = (char *)calloc(len + 4, sizeof(char));
                array_desc[0] = '[';
                array_desc[1] = 'L';
                strncpy(array_desc + 2, name, len);
                array_desc[len + 2] = ';';
                array_desc[len + 3] = '\0';
            }
            cb_push(builder, cb_type_object(array_desc));
            return;
        }
    }
    cb_push(builder, cb_type_object("[Ljava/lang/Object;"));
}

void codebuilder_build_arraylength(CodeBuilder *builder)
{
    classfile_opcode_emit_arraylength(builder->method);
    cb_pop(builder);
    cb_push(builder, cb_type_int());
}

void codebuilder_build_athrow(CodeBuilder *builder)
{
    classfile_opcode_emit_athrow(builder->method);
    cb_pop(builder);
    cb_set_stack_depth(builder, 0);
    codebuilder_mark_dead(builder);
}

void codebuilder_build_checkcast(CodeBuilder *builder, int class_index)
{
    /* Check that stack top is a reference type before checkcast */
    if (builder && builder->frame && builder->frame->stack_count > 0)
    {
        CB_VerificationType stack_top = builder->frame->stack[builder->frame->stack_count - 1];

        if (stack_top.tag != CF_VERIFICATION_OBJECT &&
            stack_top.tag != CF_VERIFICATION_NULL &&
            stack_top.tag != CF_VERIFICATION_TOP)
        {
            ConstantPoolBuilder *cp = builder->cp;
            const char *target_class = NULL;
            if (class_index < cp_builder_count(cp))
            {
                CP_Constant *c = &cp_builder_entries(cp)[class_index];
                if (c->type == CP_CONST_CLASS)
                {
                    target_class = c->u.c_class.name;
                }
            }

            fprintf(stderr, "WARNING: checkcast with non-reference type on stack at pc=%d in %s\n",
                    codebuilder_current_pc(builder),
                    builder->method_name ? builder->method_name : "<unknown>");
            fprintf(stderr, "  Stack type: %s (tag=%d)\n", cb_type_name(&stack_top), stack_top.tag);
            if (target_class)
            {
                fprintf(stderr, "  Target class: %s\n", target_class);
            }
            fprintf(stderr, "  This will cause VerifyError or ClassCastException\n");
        }
    }

    classfile_opcode_emit_checkcast(builder->method, class_index);
    cb_pop(builder);

    /* Get class name from constant pool and update stack type */
    ConstantPoolBuilder *cp = builder->cp;
    const char *class_name = NULL;
    if (class_index < cp_builder_count(cp))
    {
        CP_Constant *c = &cp_builder_entries(cp)[class_index];
        if (c->type == CP_CONST_CLASS)
        {
            class_name = c->u.c_class.name;
        }
    }

    if (class_name)
    {
        /* For arrays, class_name is already a descriptor (e.g., "[I", "[Ljava/lang/String;")
         * For other classes, build full descriptor (Lclass_name;) */
        if (class_name[0] == '[')
        {
            /* Array type - use class_name as-is */
            cb_push(builder, cb_type_object(class_name));
        }
        else
        {
            /* Non-array class - wrap in L...;*/
            int len = strlen(class_name);
            char *full_desc = (char *)calloc(len + 3, sizeof(char));
            full_desc[0] = 'L';
            strncpy(full_desc + 1, class_name, len);
            full_desc[len + 1] = ';';
            full_desc[len + 2] = '\0';
            cb_push(builder, cb_type_object(full_desc));
        }
    }
    else
    {
        cb_push(builder, cb_type_object("Ljava/lang/Object;"));
    }
}

void codebuilder_build_instanceof(CodeBuilder *builder, int class_index)
{
    classfile_opcode_emit_instanceof(builder->method, class_index);
    cb_pop(builder);
    cb_push(builder, cb_type_int());
}

void codebuilder_build_ifnull(CodeBuilder *builder, int32_t offset)
{
    classfile_opcode_emit_ifnull(builder->method, offset);
    cb_pop(builder);
    /* ifnull only pops one reference; don't reset entire stack */
}

void codebuilder_build_ifnonnull(CodeBuilder *builder, int32_t offset)
{
    classfile_opcode_emit_ifnonnull(builder->method, offset);
    cb_pop(builder);
    /* ifnonnull only pops one reference; don't reset entire stack */
}
