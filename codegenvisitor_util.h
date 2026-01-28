#pragma once

#include "ast.h"
#include "classfile.h" /* for CF_ValueTag */

typedef struct CodegenVisitor_tag CodegenVisitor;

/* Array type utilities */
int count_array_dimensions(TypeSpecifier *type);
TypeSpecifier *array_element_type(TypeSpecifier *type);
int array_length_from_type(TypeSpecifier *type);
int newarray_type_code(TypeSpecifier *element_type);
void cg_emit_newarray_for_type(CodegenVisitor *cg, TypeSpecifier *element_type);
void cg_emit_array_store_for_type(CodegenVisitor *cg, TypeSpecifier *element_type);

/* Local variable utilities */
int allocate_temp_local(CodegenVisitor *v);
int allocate_temp_local_for_tag(CodegenVisitor *v, CF_ValueTag tag);

/* Class and field lookup utilities */
int find_class_index(CodegenVisitor *v, const char *name);
int find_field_index(CodegenVisitor *v, int class_idx, const char *field_name);
/* Get class name for struct type (handles both named and anonymous structs) */
const char *cg_get_struct_class_name(CodegenVisitor *cg, TypeSpecifier *type);

/* Attribute and function utilities */
AttributeSpecifier *find_attribute(AttributeSpecifier *attr, CS_AttributeKind kind);
const char *resolve_function_name(FunctionDeclaration *func);
bool cg_is_jvm_main_function(FunctionDeclaration *func);
bool cg_main_has_argc_argv(FunctionDeclaration *func);
const char *cg_function_descriptor(FunctionDeclaration *func);

/* Checkcast utilities */
/* Generate checkcast for a pointer type.
 * Extracts class name from JVM descriptor and emits checkcast.
 * Stack: [Object] -> [typed_ref]
 * Only generates checkcast for reference types (L...;) */
void cg_emit_checkcast_for_pointer_type(CodegenVisitor *cg, TypeSpecifier *ptr_type);

/* Struct copy utilities */
/* Generate deep copy of struct on stack.
 * Stack: [src_ref] -> [new_ref]
 * Creates new object and copies all fields from source.
 * For non-struct types, this is a no-op (value already on stack). */
void cg_emit_struct_deep_copy(CodegenVisitor *v, TypeSpecifier *type);

/* Generate deep copy of array on stack.
 * Stack: [src_array] -> [new_array]
 * Creates new array and copies all elements from source.
 * If source is null, returns null.
 * element_type: the element type of the array */
void cg_emit_array_deep_copy(CodegenVisitor *v, TypeSpecifier *element_type);

/* Generate struct from initializer values on stack.
 * Stack: [val_0, val_1, ..., val_n-1] -> [struct_ref]
 * Creates new struct instance and assigns fields from stack values.
 * field_indices: array of field indices for each value (NULL = positional order)
 * value_count: number of values on stack
 * value_types: array of TypeSpecifier* for each value (NULL = no type conversion) */
void cg_emit_struct_from_init_values(CodegenVisitor *cg, const char *struct_name,
                                     int *field_indices, int value_count,
                                     TypeSpecifier **value_types);
