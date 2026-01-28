#pragma once

#include "ast.h"
#include "synthetic_codegen.h"

/* Forward declarations */
typedef struct CodegenVisitor_tag CodegenVisitor;
typedef struct CodeBuilder_tag CodeBuilder;

/*
 * Pointer Code Generation Helpers
 *
 * These functions emit inline JVM bytecode for pointer operations
 * using pointer wrapper classes (__intPtr, __charPtr, etc.).
 */

/* Low-level ptr_create bytecode (for synthetic main in codegen.c) */
void codebuilder_emit_ptr_create_bytecode(CodeBuilder *cb, int class_idx, int init_idx,
                                          int base_field, int offset_field);

/* Emit ptr create: (base_array, offset) -> PtrWrapper */
void cg_emit_ptr_create(CodegenVisitor *cg, TypeSpecifier *ptr_type);

/* Emit ptr create by type index: (base_array, offset) -> PtrWrapper */
void cg_emit_ptr_create_by_type_index(CodegenVisitor *cg, PtrTypeIndex type_idx);

/* Emit ptr add: (PtrWrapper, int_offset) -> PtrWrapper */
void cg_emit_ptr_add(CodegenVisitor *cg, TypeSpecifier *ptr_type);

/* Emit ptr diff: (PtrWrapper, PtrWrapper) -> int */
void cg_emit_ptr_diff(CodegenVisitor *cg, TypeSpecifier *ptr_type);

/* Emit ptr deref: (PtrWrapper) -> element_value */
void cg_emit_ptr_deref(CodegenVisitor *cg, TypeSpecifier *ptr_type);

/* Emit ptr store: (PtrWrapper, element_value) -> void */
void cg_emit_ptr_store(CodegenVisitor *cg, TypeSpecifier *ptr_type);

/* Emit ptr subscript: (PtrWrapper, int_index) -> element_value */
void cg_emit_ptr_subscript(CodegenVisitor *cg, TypeSpecifier *ptr_type);

/* Emit ptr store subscript: (PtrWrapper, int_index, element_value) -> void */
void cg_emit_ptr_store_subscript(CodegenVisitor *cg, TypeSpecifier *ptr_type);

/* Emit getfield for ptr.base: (PtrWrapper) -> base_array */
void cg_emit_ptr_get_base(CodegenVisitor *cg, TypeSpecifier *ptr_type);

/* Emit getfield for ptr.offset: (PtrWrapper) -> int */
void cg_emit_ptr_get_offset(CodegenVisitor *cg, TypeSpecifier *ptr_type);

/* Emit ptr clone (deep copy): (PtrWrapper) -> new PtrWrapper with same base and offset */
void cg_emit_ptr_clone(CodegenVisitor *cg, TypeSpecifier *ptr_type);
