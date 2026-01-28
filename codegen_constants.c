#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constant_pool.h"
#include "ast.h"
#include "codegen_constants.h"
#include "codegenvisitor.h"
#include "cminor_type.h"
#include "codegen_jvm_types.h"
#include "codegenvisitor_util.h"
#include "util.h"

static ConstantPoolBuilder *get_cp(CodegenVisitor *v)
{
    return code_output_cp(v->output);
}

/*
 * Add a method reference to the constant pool.
 * Uses FunctionDeclaration to get name, class, and descriptor.
 * Returns the CP index.
 */
int cg_add_method(CodegenVisitor *v, FunctionDeclaration *func)
{
    if (!func)
    {
        fprintf(stderr, "cg_add_method: func is NULL\n");
        exit(1);
    }

    const char *class_name = func->class_name ? func->class_name : v->current_class_name;
    const char *name = resolve_function_name(func);
    const char *descriptor = cg_function_descriptor(func);
    int argc = cs_count_parameters(func->param);
    if (func->is_variadic)
    {
        argc += 1;
    }

    return cp_builder_add_methodref_typed(get_cp(v), class_name, name, descriptor, func, argc);
}

int cg_find_or_add_field(CodegenVisitor *v, Declaration *decl)
{
    if (!decl)
    {
        fprintf(stderr, "declaration is NULL for field constant\n");
        exit(1);
    }

    const char *class_name = decl->class_name ? decl->class_name : v->current_class_name;
    const char *descriptor = decl->type ? cg_jvm_descriptor(decl->type) : "I";

    return cp_builder_add_fieldref(get_cp(v), class_name, decl->name, descriptor);
}

int cg_find_or_add_struct_field(CodegenVisitor *v, const char *class_name,
                                const char *field_name, int field_index,
                                TypeSpecifier *field_type)
{
    const char *desc = NULL;

    if (field_type)
    {
        desc = cg_jvm_descriptor(field_type);
    }
    else
    {
        /* Look up class definition and get field descriptor from there */
        for (int i = 0; i < v->class_def_count; ++i)
        {
            if (v->class_defs[i].name && class_name &&
                strcmp(v->class_defs[i].name, class_name) == 0)
            {
                if (field_index < v->class_defs[i].field_count)
                {
                    CG_ClassField *field = &v->class_defs[i].fields[field_index];
                    if (field->type_spec)
                    {
                        field_type = field->type_spec;
                        desc = cg_jvm_descriptor(field->type_spec);
                    }
                }
                break;
            }
        }
    }

    if (!desc)
    {
        desc = "I"; /* Default to int */
    }

    return cp_builder_add_fieldref(get_cp(v), class_name, field_name, desc);
}

int cg_find_or_add_class(CodegenVisitor *v, const char *class_name,
                         int class_index)
{
    (void)class_index; /* No longer used */
    return cp_builder_add_class(get_cp(v), class_name);
}

int cg_find_or_add_object_class(CodegenVisitor *v)
{
    return cp_builder_add_class(get_cp(v), "java/lang/Object");
}

int cg_find_or_add_array_class(CodegenVisitor *v, TypeSpecifier *element_type)
{
    /* Build array class name for CONSTANT_Class_info: "[" + element_descriptor
     * e.g., int -> "[I", String -> "[Ljava/lang/String;" */
    const char *elem_descriptor = cg_jvm_descriptor(element_type);
    int len = strlen(elem_descriptor);
    char *array_class_name = (char *)calloc(len + 2, sizeof(char));
    array_class_name[0] = '[';
    strcpy(array_class_name + 1, elem_descriptor);
    return cp_builder_add_class(get_cp(v), array_class_name);
}
