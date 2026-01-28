#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codegenvisitor.h"
#include "codegenvisitor_util.h"
#include "codebuilder_frame.h"
#include "codebuilder_part1.h"
#include "codebuilder_part2.h"
#include "codebuilder_part3.h"
#include "codebuilder_types.h"
#include "codebuilder_label.h"
#include "codebuilder_ptr.h"
#include "cminor_type.h"
#include "codegen_constants.h"
#include "codegen_jvm_types.h"
#include "code_output.h"
#include "definitions.h"
#include "synthetic_codegen.h"

int find_class_index(CodegenVisitor *v, const char *name)
{
    if (!name)
    {
        return -1;
    }
    for (int i = 0; i < v->class_def_count; ++i)
    {
        if (v->class_defs[i].name && strcmp(v->class_defs[i].name, name) == 0)
        {
            return i;
        }
    }
    return -1;
}

int find_field_index(CodegenVisitor *v, int class_idx, const char *field_name)
{
    if (!field_name || class_idx >= v->class_def_count)
    {
        return -1;
    }
    CG_ClassDef *cd = &v->class_defs[class_idx];
    for (int i = 0; i < cd->field_count; ++i)
    {
        if (cd->fields[i].name && strcmp(cd->fields[i].name, field_name) == 0)
        {
            return i;
        }
    }
    return -1;
}

const char *cg_get_struct_class_name(CodegenVisitor *cg, TypeSpecifier *type)
{
    (void)cg; /* All structs now have names directly */
    if (!type)
    {
        fprintf(stderr, "cg_get_struct_class_name: type is NULL\n");
        exit(1);
    }
    /* All structs now have names (including anonymous ones like "Foo$0") */
    return cs_type_user_type_name(type);
}

AttributeSpecifier *find_attribute(AttributeSpecifier *attr, CS_AttributeKind kind)
{
    for (; attr; attr = attr->next)
    {
        if (attr->kind == kind)
        {
            return attr;
        }
    }
    return NULL;
}

const char *resolve_function_name(FunctionDeclaration *func)
{
    return func ? func->name : NULL;
}

int count_array_dimensions(TypeSpecifier *type)
{
    int dims = 0;
    for (TypeSpecifier *p = type; p && cs_type_is_array(p); p = cs_type_child(p))
    {
        ++dims;
    }
    return dims;
}

TypeSpecifier *array_element_type(TypeSpecifier *type)
{
    if (!type || !cs_type_is_array(type))
    {
        return NULL;
    }
    return cs_type_child(type);
}

int array_length_from_type(TypeSpecifier *type)
{
    if (!type || !cs_type_is_array(type))
    {
        return 0;
    }

    Expression *size_expr = cs_type_array_size(type);
    if (!size_expr)
    {
        return 0;
    }

    if (size_expr->kind == INT_EXPRESSION)
    {
        return size_expr->u.int_value;
    }
    if (size_expr->kind == BOOL_EXPRESSION)
    {
        return size_expr->u.bool_value ? 1u : 0u;
    }

    return 0;
}

int newarray_type_code(TypeSpecifier *element_type)
{
    if (cs_type_is_double_exact(element_type))
    {
        return 7; // T_DOUBLE
    }
    if (cs_type_is_float_exact(element_type))
    {
        return 6; // T_FLOAT
    }
    if (cs_type_is_long_exact(element_type))
    {
        return 11; // T_LONG
    }
    if (cs_type_is_char_exact(element_type))
    {
        return 8; // T_BYTE (char -> byte in Java)
    }
    if (cs_type_is_short_exact(element_type))
    {
        return 9; // T_SHORT
    }
    if (cs_type_is_bool(element_type))
    {
        return 4; // T_BOOLEAN
    }

    return 10; // T_INT
}

void cg_emit_newarray_for_type(CodegenVisitor *cg, TypeSpecifier *element_type)
{
    int type_code = newarray_type_code(element_type);
    codebuilder_build_newarray(cg->builder, type_code);
}

void cg_emit_array_store_for_type(CodegenVisitor *cg, TypeSpecifier *element_type)
{
    if (cs_type_is_double_exact(element_type))
    {
        codebuilder_build_dastore(cg->builder);
    }
    else if (cs_type_is_float_exact(element_type))
    {
        codebuilder_build_fastore(cg->builder);
    }
    else if (cs_type_is_long_exact(element_type))
    {
        codebuilder_build_lastore(cg->builder);
    }
    else if (cs_type_is_char_exact(element_type))
    {
        codebuilder_build_bastore(cg->builder);
    }
    else if (cs_type_is_short_exact(element_type))
    {
        codebuilder_build_sastore(cg->builder);
    }
    else if (cs_type_is_pointer(element_type) || cs_type_is_array(element_type) ||
             (cs_type_is_named(element_type) && cs_type_is_basic_struct_or_union(element_type)))
    {
        codebuilder_build_aastore(cg->builder);
    }
    else
    {
        codebuilder_build_iastore(cg->builder);
    }
}

int allocate_temp_local(CodegenVisitor *v)
{
    /* Allocate temporary local for int type (Javac-style) */
    return codebuilder_allocate_local(v->builder, cb_type_int());
}

int allocate_temp_local_for_tag(CodegenVisitor *v, CF_ValueTag tag)
{
    CB_VerificationType vtype;
    switch (tag)
    {
    case CF_VAL_INT:
        vtype = cb_type_int();
        break;
    case CF_VAL_LONG:
        vtype = cb_type_long();
        break;
    case CF_VAL_FLOAT:
        vtype = cb_type_float();
        break;
    case CF_VAL_DOUBLE:
        vtype = cb_type_double();
        break;
    case CF_VAL_OBJECT:
        vtype = cb_type_object("Ljava/lang/Object;");
        break;
    default:
        fprintf(stderr, "allocate_temp_local_for_tag: invalid tag %d in %s\n",
                tag, v->builder->method_name ? v->builder->method_name : "<unknown>");
        exit(1);
    }
    return codebuilder_allocate_local(v->builder, vtype);
}

bool cg_is_jvm_main_function(FunctionDeclaration *func)
{
    return func && func->name && strcmp(func->name, "main") == 0;
}

/* Check if main takes (int argc, char *argv[]) */
bool cg_main_has_argc_argv(FunctionDeclaration *func)
{
    if (!func || !func->param)
    {
        return false;
    }

    /* Check for exactly 2 parameters */
    ParameterList *p1 = func->param;
    if (!p1 || !p1->next || p1->next->next)
    {
        return false;
    }

    ParameterList *p2 = p1->next;

    /* First param should be int (argc) */
    if (!cs_type_is_int_exact(p1->type))
    {
        return false;
    }

    /* Second param should be char *[] or char ** (argv) */
    if (!p2->type)
    {
        return false;
    }

    /* Check for char *[] (array of char pointers) */
    if (cs_type_is_array(p2->type) && cs_type_child(p2->type) &&
        cs_type_is_pointer(cs_type_child(p2->type)) &&
        cs_type_is_char_exact(cs_type_child(cs_type_child(p2->type))))
    {
        return true;
    }

    /* Check for char ** (pointer to char pointer) */
    if (cs_type_is_pointer(p2->type) && cs_type_child(p2->type) &&
        cs_type_is_pointer(cs_type_child(p2->type)) &&
        cs_type_is_char_exact(cs_type_child(cs_type_child(p2->type))))
    {
        return true;
    }

    return false;
}

const char *cg_function_descriptor(FunctionDeclaration *func)
{
    if (!func)
    {
        return "()V";
    }

    if (cg_is_jvm_main_function(func))
    {
        return cg_main_has_argc_argv(func) ? "(I[L__charPtr;)I" : "()I";
    }

    return cg_jvm_method_descriptor(func);
}

/* Check if a type represents an embedded struct (not Java class, not pointer) */
static bool is_embedded_struct_type(TypeSpecifier *type)
{
    if (!type)
        return false;
    if (cs_type_is_pointer(type) || cs_type_is_array(type))
        return false;
    if (!cs_type_is_basic_struct_or_union(type))
        return false;
    const char *name = cs_type_user_type_name(type);
    if (!name || !name[0])
        return false;
    /* Exclude Java standard classes */
    if (strncmp(name, "java/", 5) == 0)
        return false;
    /* Exclude pointer wrapper classes (__charPtr, __intPtr, __objectPtr, etc.) */
    if (strncmp(name, "__", 2) == 0)
        return false;
    return true;
}

static bool is_pointer_wrapper_type(TypeSpecifier *type)
{
    return type && cs_type_is_pointer(type);
}

static char *extract_class_name_from_type(TypeSpecifier *type)
{
    if (!type)
    {
        fprintf(stderr, "extract_class_name_from_type: type is NULL\n");
        exit(1);
    }
    if (cs_type_is_pointer(type))
    {
        PtrTypeIndex ptr_index = (PtrTypeIndex)cg_pointer_runtime_kind(type);
        return strdup(ptr_type_class_name(ptr_index));
    }
    const char *name = cs_type_user_type_name(type);
    if (!name || !name[0])
        return NULL;
    return strdup(name);
}

void cg_emit_checkcast_for_pointer_type(CodegenVisitor *cg, TypeSpecifier *ptr_type)
{
    if (!ptr_type)
        return;

    if (cs_type_is_pointer(ptr_type))
    {
        PtrTypeIndex ptr_index = (PtrTypeIndex)cg_pointer_runtime_kind(ptr_type);
        const char *class_name = ptr_type_class_name(ptr_index);
        int class_idx = cp_builder_add_class(code_output_cp(cg->output), class_name);
        codebuilder_build_checkcast(cg->builder, class_idx);
        return;
    }

    if (cs_type_is_array(ptr_type))
    {
        const char *class_name = cg_jvm_class_name(ptr_type);
        if (!class_name)
            return;
        /* Array type - class_name is already in correct format (e.g., "[I") */
        int class_idx = cp_builder_add_class(code_output_cp(cg->output), class_name);
        codebuilder_build_checkcast(cg->builder, class_idx);
    }
}

void cg_emit_struct_deep_copy(CodegenVisitor *v, TypeSpecifier *type)
{
    /* Only generate deep copy for struct/union types.
     * Skip enum types and typedef aliases for primitive types (e.g., uint32_t).
     * cs_type_is_basic_struct_or_union checks if basic_type is CS_STRUCT_TYPE or CS_UNION_TYPE. */
    if (!type || !cs_type_is_named(type) || !cs_type_is_basic_struct_or_union(type))
    {
        return;
    }

    const char *struct_name = cs_type_user_type_name(type);
    if (!struct_name)
    {
        return;
    }

    int class_idx = find_class_index(v, struct_name);
    if (class_idx == -1)
    {
        fprintf(stderr, "error: struct class not found for deep copy: %s\n", struct_name);
        exit(1);
    }

    CG_ClassDef *class_def = &v->class_defs[class_idx];

    /* Begin temp scope for deep copy locals */
    codebuilder_begin_block(v->builder);

    /* Stack: [src_ref]
     * Generate deep copy:
     *   astore temp_src
     *   new StructName
     *   dup
     *   invokespecial <init>
     *   astore temp_new
     *   for each field:
     *     aload temp_new
     *     aload temp_src
     *     getfield field
     *     putfield field
     *   aload temp_new
     */

    int temp_src = codebuilder_allocate_local(v->builder, cb_type_object("Ljava/lang/Object;"));
    int temp_new = codebuilder_allocate_local(v->builder, cb_type_object("Ljava/lang/Object;"));

    /* Save source reference */
    codebuilder_build_astore(v->builder, temp_src);

    /* Create new object */
    int class_const_idx = cg_find_or_add_class(v, struct_name, class_idx);
    codebuilder_build_new(v->builder, class_const_idx);
    codebuilder_build_dup(v->builder);
    int init_idx = cp_builder_add_methodref(code_output_cp(v->output),
                                            struct_name, "<init>", "()V");
    codebuilder_build_invokespecial(v->builder, init_idx);

    /* Save new object reference */
    codebuilder_build_astore(v->builder, temp_new);

    /* Copy each field (recursively deep copy embedded structs) */
    for (int i = 0; i < class_def->field_count; i++)
    {
        CG_ClassField *field = &class_def->fields[i];
        int field_const_idx = cg_find_or_add_struct_field(v, struct_name,
                                                          field->name, i, NULL);

        /* Check if this is an embedded struct that needs recursive deep copy */
        if (is_embedded_struct_type(field->type_spec))
        {
            /* Load destination object first (for putfield at the end) */
            codebuilder_build_aload(v->builder, temp_new);

            /* Load source field value */
            codebuilder_build_aload(v->builder, temp_src);
            codebuilder_build_getfield(v->builder, field_const_idx);
            /* Stack: [dest, src_field_ref] */

            /* Recursively deep copy the embedded struct */
            cg_emit_struct_deep_copy(v, field->type_spec);
            /* Stack: [dest, new_field_ref] */

            /* Store to destination field */
            codebuilder_build_putfield(v->builder, field_const_idx);
            /* Stack: [] */
            continue;
        }

        /* Check if this is an array field that needs deep copy */
        if (cs_type_is_array(field->type_spec))
        {
            /* Load destination object first (for putfield at the end) */
            codebuilder_build_aload(v->builder, temp_new);

            /* Load source field value (array reference) */
            codebuilder_build_aload(v->builder, temp_src);
            codebuilder_build_getfield(v->builder, field_const_idx);
            /* Stack: [dest, src_array] */

            /* Deep copy the array */
            TypeSpecifier *elem_type = cs_type_child(field->type_spec);
            cg_emit_array_deep_copy(v, elem_type);
            /* Stack: [dest, new_array] */

            /* Store to destination field */
            codebuilder_build_putfield(v->builder, field_const_idx);
            continue;
        }

        /* Check if this is a pointer wrapper field that needs clone */
        if (is_pointer_wrapper_type(field->type_spec))
        {
            /* Load destination object first (for putfield at the end) */
            codebuilder_build_aload(v->builder, temp_new);

            /* Load source field value (pointer wrapper) */
            codebuilder_build_aload(v->builder, temp_src);
            codebuilder_build_getfield(v->builder, field_const_idx);
            /* Stack: [dest, src_ptr] */

            /* Clone the pointer wrapper */
            cg_emit_ptr_clone(v, field->type_spec);
            /* Stack: [dest, new_ptr] */

            /* Store to destination field */
            codebuilder_build_putfield(v->builder, field_const_idx);
            continue;
        }

        /* Non-embedded field: simple copy (primitives) */
        codebuilder_build_aload(v->builder, temp_new);
        codebuilder_build_aload(v->builder, temp_src);
        codebuilder_build_getfield(v->builder, field_const_idx);
        codebuilder_build_putfield(v->builder, field_const_idx);
    }

    /* Leave new object reference on stack */
    codebuilder_build_aload(v->builder, temp_new);

    /* End temp scope - locals can be reused */
    codebuilder_end_block(v->builder);
}

/* Check if element type requires deep copy (struct, pointer, or nested array) */
static bool element_needs_deep_copy(TypeSpecifier *element_type)
{
    if (!element_type)
        return false;
    if (cs_type_is_primitive(element_type) || cs_type_is_enum(element_type))
        return false;
    /* Struct, pointer, or array elements need deep copy */
    return is_embedded_struct_type(element_type) ||
           is_pointer_wrapper_type(element_type) ||
           cs_type_is_array(element_type);
}

void cg_emit_array_deep_copy(CodegenVisitor *v, TypeSpecifier *element_type)
{
    /* Stack: [src_array] -> [new_array]
     * If source is null, returns null.
     * For primitive/enum: uses System.arraycopy (shallow copy is correct)
     * For struct/pointer/array: loops and deep copies each element
     */

    CB_Label *null_label = codebuilder_create_label(v->builder);
    CB_Label *end_label = codebuilder_create_label(v->builder);

    /* Stack: [src_array] */
    codebuilder_build_dup(v->builder);                /* [src, src] */
    codebuilder_jump_if_null(v->builder, null_label); /* [src] */

    /* Not null case */
    codebuilder_begin_block(v->builder);

    /* Build array type descriptor from element type */
    const char *elem_desc = cg_jvm_descriptor(element_type);
    int desc_len = strlen(elem_desc);
    char *array_desc = (char *)calloc(desc_len + 2, sizeof(char));
    array_desc[0] = '[';
    strcpy(array_desc + 1, elem_desc);

    int src_local = codebuilder_allocate_local(v->builder, cb_type_object(array_desc));
    int new_local = codebuilder_allocate_local(v->builder, cb_type_object(array_desc));

    /* Stack: [src_array] */
    codebuilder_build_astore(v->builder, src_local); /* [] */

    /* Create new array */
    codebuilder_build_aload(v->builder, src_local); /* [src] */
    codebuilder_build_arraylength(v->builder);      /* [len] */

    /* Create new array based on element type */
    if (cs_type_is_primitive(element_type) || cs_type_is_enum(element_type))
    {
        cg_emit_newarray_for_type(v, element_type);
    }
    else
    {
        const char *elem_class = NULL;
        if (cs_type_is_basic_struct_or_union(element_type))
        {
            elem_class = cs_type_user_type_name(element_type);
        }
        else if (cs_type_is_pointer(element_type))
        {
            PtrTypeIndex ptr_index = (PtrTypeIndex)cg_pointer_runtime_kind(element_type);
            elem_class = ptr_type_class_name(ptr_index);
        }
        else if (cs_type_is_array(element_type))
        {
            /* Nested array - element class is the array descriptor itself */
            elem_class = cg_jvm_class_name(element_type);
        }
        if (!elem_class)
        {
            elem_class = "java/lang/Object";
        }
        int class_idx = cp_builder_add_class(code_output_cp(v->output), elem_class);
        codebuilder_build_anewarray(v->builder, class_idx);
    }
    /* Stack: [new_array] */
    codebuilder_build_astore(v->builder, new_local); /* [] */

    /* Copy elements */
    if (element_needs_deep_copy(element_type))
    {
        /* Deep copy: loop and copy each element individually */
        int i_local = codebuilder_allocate_local(v->builder, cb_type_int());

        CB_Label *loop_start = codebuilder_create_label(v->builder);
        CB_Label *loop_end = codebuilder_create_label(v->builder);

        /* i = 0 */
        codebuilder_build_iconst(v->builder, 0);
        codebuilder_build_istore(v->builder, i_local);

        /* loop_start: */
        codebuilder_place_label(v->builder, loop_start);

        /* if (i >= src.length) goto loop_end */
        codebuilder_build_iload(v->builder, i_local);
        codebuilder_build_aload(v->builder, src_local);
        codebuilder_build_arraylength(v->builder);
        codebuilder_jump_if_icmp(v->builder, ICMP_GE, loop_end);

        /* new_array[i] = deep_copy(src_array[i]) */
        codebuilder_build_aload(v->builder, new_local); /* [new] */
        codebuilder_build_iload(v->builder, i_local);   /* [new, i] */
        codebuilder_build_aload(v->builder, src_local); /* [new, i, src] */
        codebuilder_build_iload(v->builder, i_local);   /* [new, i, src, i] */
        codebuilder_build_aaload(v->builder);           /* [new, i, src[i]] */

        /* Deep copy the element based on its type */
        if (is_embedded_struct_type(element_type))
        {
            cg_emit_struct_deep_copy(v, element_type);
        }
        else if (is_pointer_wrapper_type(element_type))
        {
            cg_emit_ptr_clone(v, element_type);
        }
        else if (cs_type_is_array(element_type))
        {
            /* Nested array - recursively deep copy */
            TypeSpecifier *inner_elem = cs_type_child(element_type);
            cg_emit_array_deep_copy(v, inner_elem);
        }
        /* Stack: [new, i, copied_element] */

        codebuilder_build_aastore(v->builder); /* [] */

        /* i++ */
        codebuilder_build_iinc(v->builder, i_local, 1);

        /* goto loop_start */
        codebuilder_jump(v->builder, loop_start);

        /* loop_end: */
        codebuilder_place_label(v->builder, loop_end);
    }
    else
    {
        /* Shallow copy: use System.arraycopy for primitives/enums */
        codebuilder_build_aload(v->builder, src_local); /* [src] */
        codebuilder_build_iconst(v->builder, 0);        /* [src, 0] */
        codebuilder_build_aload(v->builder, new_local); /* [src, 0, new] */
        codebuilder_build_iconst(v->builder, 0);        /* [src, 0, new, 0] */
        codebuilder_build_aload(v->builder, src_local); /* [src, 0, new, 0, src] */
        codebuilder_build_arraylength(v->builder);      /* [src, 0, new, 0, len] */

        int copy_idx = cp_builder_add_methodref(code_output_cp(v->output),
                                                "java/lang/System", "arraycopy",
                                                "(Ljava/lang/Object;ILjava/lang/Object;II)V");
        codebuilder_build_invokestatic(v->builder, copy_idx);
        /* Stack: [] */
    }

    codebuilder_build_aload(v->builder, new_local); /* [new_array] */

    codebuilder_end_block(v->builder);
    codebuilder_jump(v->builder, end_label);

    /* Null case - just leave the null on stack */
    codebuilder_place_label(v->builder, null_label);
    /* Stack already has [null] from dup before jump */

    codebuilder_place_label(v->builder, end_label);
    /* Stack: [result_array] */
}

void cg_emit_struct_from_init_values(CodegenVisitor *cg, const char *struct_name,
                                     int *field_indices, int value_count,
                                     TypeSpecifier **value_types)
{
    /* Stack: [val_0, val_1, ..., val_n-1] → [struct_ref]
     * Creates struct instance, assigns fields from stack values.
     * field_indices: NULL means positional (field 0, 1, 2, ...)
     * value_types: array of TypeSpecifier* for each value (NULL = no type conversion)
     * Also initializes uninitialized embedded struct fields recursively.
     */

    /* Begin temp scope for struct_local */
    codebuilder_begin_block(cg->builder);

    int class_idx = find_class_index(cg, struct_name);
    if (class_idx == -1)
    {
        fprintf(stderr, "error: struct class not found: %s\n", struct_name);
        exit(1);
    }
    CG_ClassDef *class_def = &cg->class_defs[class_idx];

    /* Create new struct instance */
    int const_idx = cg_find_or_add_class(cg, struct_name, class_idx);
    codebuilder_build_new(cg->builder, const_idx);
    codebuilder_build_dup(cg->builder);
    int init_idx = cp_builder_add_methodref(code_output_cp(cg->output),
                                            struct_name, "<init>", "()V");
    codebuilder_build_invokespecial(cg->builder, init_idx);
    /* Stack: [val_0, val_1, ..., val_n-1, struct_ref] */

    /* Store struct ref in temp local for field assignment */
    int struct_local = codebuilder_allocate_local(cg->builder, cb_type_object("Ljava/lang/Object;"));
    codebuilder_build_astore(cg->builder, struct_local);
    /* Stack: [val_0, val_1, ..., val_n-1] */

    /* Track which fields are initialized */
    bool *field_initialized = (bool *)calloc(class_def->field_count, sizeof(bool));

    /* Assign fields in reverse order (last value on top of stack) */
    for (int i = value_count - 1; i >= 0; i--)
    {
        int fi = field_indices ? field_indices[i] : i;
        if (fi < 0 || fi >= class_def->field_count)
            continue;

        field_initialized[fi] = true;
        CG_ClassField *field = &class_def->fields[fi];

        /* Load struct ref */
        codebuilder_build_aload(cg->builder, struct_local);
        /* Stack: [val_0, ..., val_i, struct_ref] */

        /* Swap to get correct order for putfield */
        codebuilder_build_swap(cg->builder);
        /* Stack: [val_0, ..., struct_ref, val_i] */

        /* Check if array-to-pointer conversion is needed:
         * If field is pointer type and value is array type, convert. */
        if (value_types && value_types[i] && cs_type_is_pointer(field->type_spec))
        {
            TypeSpecifier *val_type = value_types[i];
            if (cs_type_is_array(val_type))
            {
                /* Stack: [struct_ref, array_ref]
                 * Need: [struct_ref, ptr_ref]
                 * Convert array to pointer wrapper:
                 *   iconst 0          ; [struct_ref, array_ref, 0]
                 *   ptr_create        ; [struct_ref, ptr_ref]
                 */
                codebuilder_build_iconst(cg->builder, 0);
                cg_emit_ptr_create(cg, field->type_spec);
            }
        }

        /* Store to field */
        int field_const_idx = cg_find_or_add_struct_field(
            cg, struct_name, field->name, fi, NULL);
        codebuilder_build_putfield(cg->builder, field_const_idx);
        /* Stack: [val_0, ...] */
    }

    /* Initialize uninitialized embedded struct fields recursively */
    for (int fi = 0; fi < class_def->field_count; fi++)
    {
        if (field_initialized[fi])
            continue;

        CG_ClassField *field = &class_def->fields[fi];
        if (!is_embedded_struct_type(field->type_spec))
            continue;

        /* This is an uninitialized embedded struct field */
        char *embedded_name = extract_class_name_from_type(field->type_spec);
        if (!embedded_name)
            continue;

        /* Recursively create the embedded struct (with no initializer values) */
        cg_emit_struct_from_init_values(cg, embedded_name, NULL, 0, NULL);
        /* Stack: [embedded_struct_ref] */

        /* Store to the field: struct_ref.field = embedded_struct_ref */
        codebuilder_build_aload(cg->builder, struct_local);
        /* Stack: [embedded_struct_ref, struct_ref] */
        codebuilder_build_swap(cg->builder);
        /* Stack: [struct_ref, embedded_struct_ref] */
        int field_const_idx = cg_find_or_add_struct_field(
            cg, struct_name, field->name, fi, NULL);
        codebuilder_build_putfield(cg->builder, field_const_idx);
        /* Stack: [] */

        free(embedded_name);
    }

    /* Initialize uninitialized pointer fields with null pointer (base=null, offset=0) */
    for (int fi = 0; fi < class_def->field_count; fi++)
    {
        if (field_initialized[fi])
            continue;

        CG_ClassField *field = &class_def->fields[fi];
        if (!is_pointer_wrapper_type(field->type_spec))
            continue;

        /* This is an uninitialized pointer field - create null pointer */
        char *ptr_class_name = extract_class_name_from_type(field->type_spec);
        if (!ptr_class_name)
            continue;

        /* Create null pointer: new __XxxPtr() with base=null, offset=0 */
        int ptr_class_idx = cg_find_or_add_class(cg, ptr_class_name, -1);
        codebuilder_build_new(cg->builder, ptr_class_idx);
        codebuilder_build_dup(cg->builder);
        int ptr_init_idx = cp_builder_add_methodref(code_output_cp(cg->output),
                                                    ptr_class_name, "<init>", "()V");
        codebuilder_build_invokespecial(cg->builder, ptr_init_idx);
        /* Stack: [ptr_ref] - base and offset are null/0 by default Java initialization */

        /* Store to the field: struct_ref.field = ptr_ref */
        codebuilder_build_aload(cg->builder, struct_local);
        /* Stack: [ptr_ref, struct_ref] */
        codebuilder_build_swap(cg->builder);
        /* Stack: [struct_ref, ptr_ref] */
        int field_const_idx = cg_find_or_add_struct_field(
            cg, struct_name, field->name, fi, NULL);
        codebuilder_build_putfield(cg->builder, field_const_idx);
        /* Stack: [] */

        free(ptr_class_name);
    }

    /* Initialize uninitialized fixed-size array fields */
    for (int fi = 0; fi < class_def->field_count; fi++)
    {
        if (field_initialized[fi])
            continue;

        CG_ClassField *field = &class_def->fields[fi];
        if (!cs_type_is_array(field->type_spec))
            continue;

        int arr_len = array_length_from_type(field->type_spec);
        if (arr_len <= 0)
            continue; /* VLA or dynamic array - skip */

        /* This is an uninitialized fixed-size array field */
        TypeSpecifier *elem_type = cs_type_child(field->type_spec);

        /* Create the array: push length, then newarray/anewarray */
        codebuilder_build_iconst(cg->builder, arr_len);
        if (cs_type_is_primitive(elem_type) || cs_type_is_enum(elem_type))
        {
            cg_emit_newarray_for_type(cg, elem_type);
        }
        else
        {
            /* Reference type array (struct, pointer, nested array) */
            const char *elem_class = NULL;
            if (cs_type_is_basic_struct_or_union(elem_type))
            {
                elem_class = cs_type_user_type_name(elem_type);
            }
            else if (cs_type_is_pointer(elem_type))
            {
                PtrTypeIndex ptr_index = (PtrTypeIndex)cg_pointer_runtime_kind(elem_type);
                elem_class = ptr_type_class_name(ptr_index);
            }
            else if (cs_type_is_array(elem_type))
            {
                elem_class = cg_jvm_class_name(elem_type);
            }
            if (!elem_class)
            {
                elem_class = "java/lang/Object";
            }
            int class_idx = cp_builder_add_class(code_output_cp(cg->output), elem_class);
            codebuilder_build_anewarray(cg->builder, class_idx);
        }
        /* Stack: [array_ref] */

        /* For struct arrays, initialize each element with a new struct instance */
        if (is_embedded_struct_type(elem_type))
        {
            const char *elem_struct_name = cs_type_user_type_name(elem_type);
            int arr_local = codebuilder_allocate_local(cg->builder, cb_type_object("Ljava/lang/Object;"));
            codebuilder_build_astore(cg->builder, arr_local);
            /* Stack: [] */

            /* Loop: for (int i = 0; i < arr_len; i++) arr[i] = new Elem(); */
            for (int i = 0; i < arr_len; i++)
            {
                codebuilder_build_aload(cg->builder, arr_local); /* [arr] */
                codebuilder_build_iconst(cg->builder, i);        /* [arr, i] */
                cg_emit_struct_from_init_values(cg, elem_struct_name, NULL, 0, NULL);
                /* [arr, i, elem] */
                codebuilder_build_aastore(cg->builder); /* [] */
            }

            codebuilder_build_aload(cg->builder, arr_local);
            /* Stack: [array_ref] */
        }

        /* Store to the field: struct_ref.field = array_ref */
        codebuilder_build_aload(cg->builder, struct_local);
        /* Stack: [array_ref, struct_ref] */
        codebuilder_build_swap(cg->builder);
        /* Stack: [struct_ref, array_ref] */
        int field_const_idx = cg_find_or_add_struct_field(
            cg, struct_name, field->name, fi, NULL);
        codebuilder_build_putfield(cg->builder, field_const_idx);
        /* Stack: [] */
    }

    free(field_initialized);

    /* Load struct ref to leave on stack */
    codebuilder_build_aload(cg->builder, struct_local);
    /* Stack: [struct_ref] */

    /* End temp scope - struct_local slot can be reused */
    codebuilder_end_block(cg->builder);
}
