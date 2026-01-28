#pragma once

#include "codegenvisitor.h"

/* Add method reference using FunctionDeclaration info */
int cg_add_method(CodegenVisitor *v, FunctionDeclaration *func);

int cg_find_or_add_field(CodegenVisitor *v, Declaration *decl);
int cg_find_or_add_struct_field(CodegenVisitor *v, const char *class_name,
                                const char *field_name, int field_index,
                                TypeSpecifier *field_type);
int cg_find_or_add_class(CodegenVisitor *v, const char *class_name, int class_index);
int cg_find_or_add_object_class(CodegenVisitor *v);

/**
 * Add array type class descriptor to constant pool for ANEWARRAY.
 * element_type should be the element type (e.g., int for int[], int[] for int[][]).
 * Returns constant pool index for the array class.
 */
int cg_find_or_add_array_class(CodegenVisitor *v, TypeSpecifier *element_type);
