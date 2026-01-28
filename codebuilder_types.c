/*
 * CodeBuilder Types - Verification Type System
 *
 * Handles:
 * - Verification type constructors
 * - Type comparison and slot counting
 * - C type to JVM type conversion
 * - Descriptor parsing
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codebuilder_types.h"
#include "codebuilder_internal.h"
#include "cminor_type.h"
#include "codegen_jvm_types.h"
#include "classfile_opcode.h"

/* ============================================================
 * Verification Type Constructors
 * ============================================================ */

CB_VerificationType cb_type_int()
{
    CB_VerificationType t = {};
    t.tag = CF_VERIFICATION_INTEGER;
    return t;
}

CB_VerificationType cb_type_long()
{
    CB_VerificationType t = {};
    t.tag = CF_VERIFICATION_LONG;
    return t;
}

CB_VerificationType cb_type_float()
{
    CB_VerificationType t = {};
    t.tag = CF_VERIFICATION_FLOAT;
    return t;
}

CB_VerificationType cb_type_double()
{
    CB_VerificationType t = {};
    t.tag = CF_VERIFICATION_DOUBLE;
    return t;
}

CB_VerificationType cb_type_null()
{
    CB_VerificationType t = {};
    t.tag = CF_VERIFICATION_NULL;
    return t;
}

CB_VerificationType cb_type_top()
{
    CB_VerificationType t = {};
    t.tag = CF_VERIFICATION_TOP;
    return t;
}

CB_VerificationType cb_type_object(const char *class_name)
{
    CB_VerificationType t = {};
    t.tag = CF_VERIFICATION_OBJECT;
    t.u.class_name = class_name;
    return t;
}

CB_VerificationType cb_type_uninitialized(int offset)
{
    CB_VerificationType t = {};
    t.tag = CF_VERIFICATION_UNINITIALIZED;
    t.u.offset = offset;
    return t;
}

CB_VerificationType cb_type_uninitialized_this()
{
    CB_VerificationType t = {};
    t.tag = CF_VERIFICATION_UNINITIALIZED_THIS;
    return t;
}

int cb_type_slots(CB_VerificationType *type)
{
    if (!type)
    {
        fprintf(stderr, "cb_type_slots: type is NULL\n");
        exit(1);
    }

    switch (type->tag)
    {
    case CF_VERIFICATION_LONG:
    case CF_VERIFICATION_DOUBLE:
        return 2;
    default:
        return 1;
    }
}

/* Check if type is a reference type (can be stored with astore) */
bool cb_type_is_reference(CB_VerificationType *type)
{
    if (!type)
    {
        fprintf(stderr, "cb_type_is_reference: type is NULL\n");
        exit(1);
    }

    switch (type->tag)
    {
    case CF_VERIFICATION_OBJECT:
    case CF_VERIFICATION_NULL:
    case CF_VERIFICATION_UNINITIALIZED:
    case CF_VERIFICATION_UNINITIALIZED_THIS:
        return true;
    default:
        return false;
    }
}

/* Check if type is an integer type (can be stored with istore) */
bool cb_type_is_integer(CB_VerificationType *type)
{
    if (!type)
    {
        fprintf(stderr, "cb_type_is_integer: type is NULL\n");
        exit(1);
    }

    return type->tag == CF_VERIFICATION_INTEGER;
}

/* Check if type is category 1 (1 slot) */
bool cb_type_is_category1(CB_VerificationType *type)
{
    return cb_type_slots(type) == 1;
}

/* Check if type is category 2 (2 slots: long or double) */
bool cb_type_is_category2(CB_VerificationType *type)
{
    return cb_type_slots(type) == 2;
}

/* Get human-readable type name for diagnostics */
const char *cb_type_name(CB_VerificationType *type)
{
    if (!type)
    {
        return "<null>";
    }

    switch (type->tag)
    {
    case CF_VERIFICATION_TOP:
        return "top";
    case CF_VERIFICATION_INTEGER:
        return "integer";
    case CF_VERIFICATION_FLOAT:
        return "float";
    case CF_VERIFICATION_LONG:
        return "long";
    case CF_VERIFICATION_DOUBLE:
        return "double";
    case CF_VERIFICATION_NULL:
        return "null";
    case CF_VERIFICATION_UNINITIALIZED_THIS:
        return "uninitializedThis";
    case CF_VERIFICATION_OBJECT:
        return type->u.class_name ? type->u.class_name : "object";
    case CF_VERIFICATION_UNINITIALIZED:
        return "uninitialized";
    default:
        return "<unknown>";
    }
}

bool cb_type_equals(CB_VerificationType *a, CB_VerificationType *b)
{
    if (!a || !b)
    {
        return a == b;
    }

    if (a->tag != b->tag)
    {
        return false;
    }

    switch (a->tag)
    {
    case CF_VERIFICATION_OBJECT:
        if (a->u.class_name && b->u.class_name)
        {
            return strcmp(a->u.class_name, b->u.class_name) == 0;
        }
        return a->u.class_name == b->u.class_name;

    case CF_VERIFICATION_UNINITIALIZED:
        return a->u.offset == b->u.offset;

    default:
        return true;
    }
}

bool cb_type_assignable(CB_VerificationType value, CB_VerificationType target)
{
    /* TOP is always compatible (uninitialized/unknown state) */
    if (value.tag == CF_VERIFICATION_TOP || target.tag == CF_VERIFICATION_TOP)
    {
        return true;
    }

    /* NULL can be assigned to any reference type */
    if (value.tag == CF_VERIFICATION_NULL)
    {
        return target.tag == CF_VERIFICATION_OBJECT ||
               target.tag == CF_VERIFICATION_NULL ||
               target.tag == CF_VERIFICATION_UNINITIALIZED ||
               target.tag == CF_VERIFICATION_UNINITIALIZED_THIS;
    }

    /* Exact match */
    if (cb_type_equals(&value, &target))
    {
        return true;
    }

    /* Reference type compatibility */
    if (value.tag == CF_VERIFICATION_OBJECT && target.tag == CF_VERIFICATION_OBJECT)
    {
        /* Same class */
        if (value.u.class_name && target.u.class_name &&
            strcmp(value.u.class_name, target.u.class_name) == 0)
        {
            return true;
        }

        /* Array type to Object is OK in some contexts */
        if (target.u.class_name && strcmp(target.u.class_name, "Ljava/lang/Object;") == 0)
        {
            return true;
        }

        /* Array covariance: Foo[] is assignable to Object[] */
        if (value.u.class_name && target.u.class_name &&
            value.u.class_name[0] == '[' && target.u.class_name[0] == '[')
        {
            /* If target is Object[], any object array is assignable (array covariance) */
            if (strcmp(target.u.class_name, "[Ljava/lang/Object;") == 0 &&
                value.u.class_name[1] == 'L') /* Source is also object array */
            {
                return true;
            }
        }

        /* Different specific types are NOT assignable without explicit cast */
        return false;
    }

    /* Primitive types must match exactly */
    return false;
}

CB_VerificationType cb_type_from_c_type(TypeSpecifier *type)
{
    CB_VerificationType result = {};

    if (!type)
    {
        fprintf(stderr, "cb_vtype_from_ctype: type is NULL\n");
        exit(1);
    }

    /* Get JVM type info from codegen module */
    CG_JVMRefKind ref_kind = cg_jvm_ref_kind(type);

    switch (ref_kind)
    {
    case CG_JVM_REF_PRIMITIVE:
    {
        if (cs_type_is_double_exact(type))
        {
            result.tag = CF_VERIFICATION_DOUBLE;
        }
        else if (cs_type_is_float_exact(type))
        {
            result.tag = CF_VERIFICATION_FLOAT;
        }
        else if (cs_type_is_long_exact(type))
        {
            result.tag = CF_VERIFICATION_LONG;
        }
        else
        {
            /* int, char, bool, etc. all map to INTEGER */
            result.tag = CF_VERIFICATION_INTEGER;
        }
        break;
    }

    case CG_JVM_REF_OBJECT:
    case CG_JVM_REF_ARRAY:
    {
        result.tag = CF_VERIFICATION_OBJECT;
        /* Extract class name from JVM descriptor */
        const char *desc = cg_jvm_descriptor(type);
        if (desc)
        {
            result.u.class_name = desc;
        }
        else
        {
            fprintf(stderr, "cb_vtype_from_ctype: cg_jvm_descriptor returned NULL for CG_JVM_REF_OBJECT/ARRAY\n");
            exit(1);
        }
        break;
    }

    case CG_JVM_REF_POINTER:
    {
        result.tag = CF_VERIFICATION_OBJECT;
        /* Pointers use wrapper classes (__intPtr, __charPtr, __objectPtr, etc.)
         * Get descriptor from cg_jvm_descriptor which returns "L__objectPtr;" etc. */
        const char *desc = cg_jvm_descriptor(type);
        if (desc)
        {
            result.u.class_name = desc;
        }
        else
        {
            fprintf(stderr, "cb_vtype_from_ctype: cg_jvm_descriptor returned NULL for CG_JVM_REF_POINTER\n");
            exit(1);
        }
        break;
    }

    default:
        fprintf(stderr, "cb_vtype_from_ctype: unknown ref_kind %d\n", ref_kind);
        exit(1);
    }

    return result;
}

/* ============================================================
 * Descriptor Parsing
 * ============================================================ */

CB_VerificationType cb_descriptor_type(const char **p)
{
    CB_VerificationType type = cb_type_top();
    if (!p || !*p || !**p)
    {
        return type;
    }

    const char *start = *p;
    char c = **p;
    switch (c)
    {
    case 'L':
        while (**p && **p != ';')
        {
            (*p)++;
        }
        if (**p == ';')
        {
            (*p)++;
        }
        return cb_type_object(start);
    case '[':
        while (**p == '[')
        {
            (*p)++;
        }
        if (**p == 'L')
        {
            while (**p && **p != ';')
            {
                (*p)++;
            }
            if (**p == ';')
            {
                (*p)++;
            }
        }
        else if (**p)
        {
            (*p)++;
        }
        return cb_type_object(start);
    case 'J':
        (*p)++;
        return cb_type_long();
    case 'D':
        (*p)++;
        return cb_type_double();
    case 'F':
        (*p)++;
        return cb_type_float();
    case 'I':
    case 'B':
    case 'C':
    case 'S':
    case 'Z':
        (*p)++;
        return cb_type_int();
    case 'V':
        (*p)++;
        return cb_type_top();
    default:
        fprintf(stderr, "cb_type_from_descriptor: unknown descriptor '%c'\n", **p);
        exit(1);
    }
}

CB_VerificationType cb_type_from_value_tag(CF_ValueTag tag)
{
    switch (tag)
    {
    case CF_VAL_INT:
        return cb_type_int();
    case CF_VAL_DOUBLE:
        return cb_type_double();
    case CF_VAL_FLOAT:
        return cb_type_float();
    case CF_VAL_LONG:
        return cb_type_long();
    case CF_VAL_NULL:
        return cb_type_null();
    case CF_VAL_OBJECT:
        return cb_type_object("Ljava/lang/Object;");
    default:
        fprintf(stderr, "cb_type_from_value_tag: unknown value tag %d\n", tag);
        exit(1);
    }
}

/* Extract element type from array type
 * e.g., "[Ljava/lang/String;" -> "Ljava/lang/String;"
 *       "[[I" -> "[I"
 *       "[I" -> int type
 */
CB_VerificationType cb_type_array_element(CB_VerificationType array_type)
{
    /* If not an object type, return as-is */
    if (array_type.tag != CF_VERIFICATION_OBJECT)
    {
        return cb_type_object("Ljava/lang/Object;");
    }

    const char *desc = array_type.u.class_name;
    if (!desc || desc[0] != '[')
    {
        /* Not an array descriptor, return Object */
        return cb_type_object("Ljava/lang/Object;");
    }

    /* Skip the leading '[' to get element type */
    const char *element_desc = desc + 1;

    if (!*element_desc)
    {
        return cb_type_object("Ljava/lang/Object;");
    }

    /* Parse the element type */
    switch (element_desc[0])
    {
    case '[':
    case 'L':
        /* Reference type - return as object */
        return cb_type_object(element_desc);
    case 'B':
    case 'C':
    case 'S':
    case 'I':
    case 'Z':
        return cb_type_int();
    case 'J':
        return cb_type_long();
    case 'F':
        return cb_type_float();
    case 'D':
        return cb_type_double();
    default:
        fprintf(stderr, "cb_type_array_element: unknown element descriptor '%c'\n", element_desc[0]);
        exit(1);
    }
}

/* ============================================================
 * Invoke Descriptor Application
 * ============================================================ */

void codebuilder_apply_invoke_descriptor(CodeBuilder *builder, const char *descriptor,
                                         bool has_this)
{
    if (!builder || !builder->method || !descriptor)
    {
        return;
    }

    const char *p = strchr(descriptor, '(');
    if (!p)
    {
        return;
    }

    if (has_this)
    {
        cb_pop(builder);
    }

    ++p;
    while (*p && *p != ')')
    {
        (void)cb_descriptor_type(&p);
        cb_pop(builder);
    }

    if (*p == ')')
    {
        ++p;
        CB_VerificationType ret = cb_descriptor_type(&p);
        if (ret.tag != CF_VERIFICATION_TOP)
        {
            cb_push(builder, ret);
        }
    }
}

void codebuilder_apply_invoke_signature(CodeBuilder *builder, FunctionDeclaration *func,
                                        bool has_this)
{
    if (!builder || !builder->method || !func)
    {
        return;
    }

    if (has_this)
    {
        cb_pop(builder);
    }

    for (ParameterList *p = func->param; p; p = p->next)
    {
        if (p->is_ellipsis)
        {
            continue;
        }
        cb_pop(builder);
    }

    if (func->is_variadic)
    {
        cb_pop(builder);
    }

    if (func->type && !cs_type_is_void(func->type))
    {
        CB_VerificationType ret = cb_type_from_c_type(func->type);
        if (ret.tag != CF_VERIFICATION_TOP || ret.u.class_name)
        {
            cb_push(builder, ret);
        }
    }
}
