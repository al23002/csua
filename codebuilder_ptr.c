/*
 * Pointer Code Generation Helpers
 *
 * Emits inline JVM bytecode for pointer operations using
 * pointer wrapper classes (__intPtr, __charPtr, etc.).
 */

#include "codebuilder_ptr.h"
#include "codebuilder_part1.h"
#include "codebuilder_part2.h"
#include "codebuilder_part3.h"
#include "codegenvisitor.h"
#include "codegen_jvm_types.h"
#include "code_output.h"
#include "compiler.h"
#include "constant_pool.h"

/* Convert TypeSpecifier to PtrTypeIndex using codegen_jvm_types */
static PtrTypeIndex cg_ptr_type_index(TypeSpecifier *ptr_type)
{
    /* CG_PointerRuntimeKind and PtrTypeIndex have same values */
    return (PtrTypeIndex)cg_pointer_runtime_kind(ptr_type);
}

/* Emit appropriate array load instruction based on pointer type */
static void cg_emit_aload_for_type(CodeBuilder *builder, PtrTypeIndex type_idx)
{
    switch (type_idx)
    {
    case PTR_TYPE_CHAR:
    case PTR_TYPE_BOOL:
        codebuilder_build_baload(builder);
        break;
    case PTR_TYPE_SHORT:
        codebuilder_build_saload(builder);
        break;
    case PTR_TYPE_INT:
        codebuilder_build_iaload(builder);
        break;
    case PTR_TYPE_LONG:
        codebuilder_build_laload(builder);
        break;
    case PTR_TYPE_FLOAT:
        codebuilder_build_faload(builder);
        break;
    case PTR_TYPE_DOUBLE:
        codebuilder_build_daload(builder);
        break;
    case PTR_TYPE_OBJECT:
        codebuilder_build_aaload(builder);
        break;
    default:
        codebuilder_build_iaload(builder);
        break;
    }
}

/* Emit appropriate array store instruction based on pointer type */
static void cg_emit_astore_for_type(CodeBuilder *builder, PtrTypeIndex type_idx)
{
    switch (type_idx)
    {
    case PTR_TYPE_CHAR:
    case PTR_TYPE_BOOL:
        codebuilder_build_bastore(builder);
        break;
    case PTR_TYPE_SHORT:
        codebuilder_build_sastore(builder);
        break;
    case PTR_TYPE_INT:
        codebuilder_build_iastore(builder);
        break;
    case PTR_TYPE_LONG:
        codebuilder_build_lastore(builder);
        break;
    case PTR_TYPE_FLOAT:
        codebuilder_build_fastore(builder);
        break;
    case PTR_TYPE_DOUBLE:
        codebuilder_build_dastore(builder);
        break;
    case PTR_TYPE_OBJECT:
        codebuilder_build_aastore(builder);
        break;
    default:
        codebuilder_build_iastore(builder);
        break;
    }
}

/* Low-level ptr_create bytecode emission (for use by codegen.c synthetic main) */
void codebuilder_emit_ptr_create_bytecode(CodeBuilder *cb, int class_idx, int init_idx,
                                          int base_field, int offset_field)
{
    /* Stack: [base, offset]
     * Generate inline:
     *   new __XPtr          ; [base, offset, ptr]
     *   dup                 ; [base, offset, ptr, ptr]
     *   invokespecial <init>; [base, offset, ptr]
     *   dup_x2              ; [ptr, base, offset, ptr]
     *   swap                ; [ptr, base, ptr, offset]
     *   putfield offset     ; [ptr, base]
     *   swap                ; [base, ptr]
     *   dup_x1              ; [ptr, base, ptr]
     *   swap                ; [ptr, ptr, base]
     *   putfield base       ; [ptr]
     */
    codebuilder_build_new(cb, class_idx);
    codebuilder_build_dup(cb);
    codebuilder_build_invokespecial(cb, init_idx);
    codebuilder_build_dup_x2(cb);
    codebuilder_build_swap(cb);
    codebuilder_build_putfield(cb, offset_field);
    codebuilder_build_swap(cb);
    codebuilder_build_dup_x1(cb);
    codebuilder_build_swap(cb);
    codebuilder_build_putfield(cb, base_field);
}

void cg_emit_ptr_create_by_type_index(CodegenVisitor *cg, PtrTypeIndex type_idx)
{
    /* Mark ptr struct class usage (for selective generation) */
    ptr_usage_mark(type_idx);

    const char *class_name = ptr_type_class_name(type_idx);
    const char *base_desc = ptr_type_base_descriptor(type_idx);
    ConstantPoolBuilder *cp = code_output_cp(cg->output);

    /* Get constant pool indices */
    int class_idx = cp_builder_add_class(cp, class_name);
    int init_idx = cp_builder_add_methodref(cp, class_name, "<init>", "()V");
    int base_field = cp_builder_add_fieldref(cp, class_name, "base", base_desc);
    int offset_field = cp_builder_add_fieldref(cp, class_name, "offset", "I");

    codebuilder_emit_ptr_create_bytecode(cg->builder, class_idx, init_idx, base_field, offset_field);
}

void cg_emit_ptr_create(CodegenVisitor *cg, TypeSpecifier *ptr_type)
{
    PtrTypeIndex type_idx = cg_ptr_type_index(ptr_type);
    cg_emit_ptr_create_by_type_index(cg, type_idx);
}

void cg_emit_ptr_add(CodegenVisitor *cg, TypeSpecifier *ptr_type)
{
    PtrTypeIndex type_idx = cg_ptr_type_index(ptr_type);
    ptr_usage_mark(type_idx);

    const char *class_name = ptr_type_class_name(type_idx);
    const char *base_desc = ptr_type_base_descriptor(type_idx);
    ConstantPoolBuilder *cp = code_output_cp(cg->output);

    int class_idx = cp_builder_add_class(cp, class_name);
    int init_idx = cp_builder_add_methodref(cp, class_name, "<init>", "()V");
    int base_field = cp_builder_add_fieldref(cp, class_name, "base", base_desc);
    int offset_field = cp_builder_add_fieldref(cp, class_name, "offset", "I");

    /* Stack: [ptr, delta]
     * Result: [new_ptr] with new_ptr.base = ptr.base, new_ptr.offset = ptr.offset + delta
     *
     *   swap              ; [delta, ptr]
     *   dup               ; [delta, ptr, ptr]
     *   getfield base     ; [delta, ptr, base]
     *   dup_x1            ; [delta, base, ptr, base]
     *   pop               ; [delta, base, ptr]
     *   getfield offset   ; [delta, base, offset]
     *   dup2_x1           ; [base, offset, delta, base, offset]
     *   pop2              ; [base, offset, delta]
     *   iadd              ; [base, offset+delta]
     *   (then same as ptr_create)
     */
    codebuilder_build_swap(cg->builder);
    codebuilder_build_dup(cg->builder);
    codebuilder_build_getfield(cg->builder, base_field);
    codebuilder_build_dup_x1(cg->builder);
    codebuilder_build_pop(cg->builder);
    codebuilder_build_getfield(cg->builder, offset_field);
    codebuilder_build_dup2_x1(cg->builder);
    codebuilder_build_pop2(cg->builder);
    codebuilder_build_iadd(cg->builder);

    /* Now stack is [base, offset+delta], create new ptr */
    codebuilder_build_new(cg->builder, class_idx);
    codebuilder_build_dup(cg->builder);
    codebuilder_build_invokespecial(cg->builder, init_idx);
    codebuilder_build_dup_x2(cg->builder);
    codebuilder_build_swap(cg->builder);
    codebuilder_build_putfield(cg->builder, offset_field);
    codebuilder_build_swap(cg->builder);
    codebuilder_build_dup_x1(cg->builder);
    codebuilder_build_swap(cg->builder);
    codebuilder_build_putfield(cg->builder, base_field);
}

void cg_emit_ptr_diff(CodegenVisitor *cg, TypeSpecifier *ptr_type)
{
    PtrTypeIndex type_idx = cg_ptr_type_index(ptr_type);
    ptr_usage_mark(type_idx);

    const char *class_name = ptr_type_class_name(type_idx);
    ConstantPoolBuilder *cp = code_output_cp(cg->output);
    int offset_field = cp_builder_add_fieldref(cp, class_name, "offset", "I");

    /* Stack: [p1, p2]
     * Result: [p1.offset - p2.offset]
     *
     *   swap              ; [p2, p1]
     *   getfield offset   ; [p2, offset1]
     *   swap              ; [offset1, p2]
     *   getfield offset   ; [offset1, offset2]
     *   isub              ; [offset1 - offset2]
     */
    codebuilder_build_swap(cg->builder);
    codebuilder_build_getfield(cg->builder, offset_field);
    codebuilder_build_swap(cg->builder);
    codebuilder_build_getfield(cg->builder, offset_field);
    codebuilder_build_isub(cg->builder);
}

void cg_emit_ptr_deref(CodegenVisitor *cg, TypeSpecifier *ptr_type)
{
    PtrTypeIndex type_idx = cg_ptr_type_index(ptr_type);
    ptr_usage_mark(type_idx);

    const char *class_name = ptr_type_class_name(type_idx);
    const char *base_desc = ptr_type_base_descriptor(type_idx);
    ConstantPoolBuilder *cp = code_output_cp(cg->output);
    int base_field = cp_builder_add_fieldref(cp, class_name, "base", base_desc);
    int offset_field = cp_builder_add_fieldref(cp, class_name, "offset", "I");

    /* Stack: [ptr]
     * Result: [ptr.base[ptr.offset]]
     *
     *   dup               ; [ptr, ptr]
     *   getfield base     ; [ptr, base]
     *   swap              ; [base, ptr]
     *   getfield offset   ; [base, offset]
     *   Xaload            ; [element]
     */
    codebuilder_build_dup(cg->builder);
    codebuilder_build_getfield(cg->builder, base_field);
    codebuilder_build_swap(cg->builder);
    codebuilder_build_getfield(cg->builder, offset_field);
    cg_emit_aload_for_type(cg->builder, type_idx);
}

void cg_emit_ptr_store(CodegenVisitor *cg, TypeSpecifier *ptr_type)
{
    PtrTypeIndex type_idx = cg_ptr_type_index(ptr_type);
    ptr_usage_mark(type_idx);

    const char *class_name = ptr_type_class_name(type_idx);
    const char *base_desc = ptr_type_base_descriptor(type_idx);
    ConstantPoolBuilder *cp = code_output_cp(cg->output);
    int base_field = cp_builder_add_fieldref(cp, class_name, "base", base_desc);
    int offset_field = cp_builder_add_fieldref(cp, class_name, "offset", "I");
    bool is_wide = ptr_type_is_wide(type_idx);

    /* Stack: [ptr, value]
     * Result: [] (ptr.base[ptr.offset] = value)
     *
     * For category 1 (non-wide) values:
     *   swap              ; [value, ptr]
     *   dup               ; [value, ptr, ptr]
     *   getfield base     ; [value, ptr, base]
     *   dup_x1            ; [value, base, ptr, base]
     *   pop               ; [value, base, ptr]
     *   getfield offset   ; [value, base, offset]
     *   dup2_x1           ; [base, offset, value, base, offset]
     *   pop2              ; [base, offset, value]
     *   Xastore           ; []
     *
     * For category 2 (wide) values:
     *   dup2_x1           ; [value, ptr, value]
     *   pop2              ; [value, ptr]
     *   dup               ; [value, ptr, ptr]
     *   getfield base     ; [value, ptr, base]
     *   dup_x1            ; [value, base, ptr, base]
     *   pop               ; [value, base, ptr]
     *   getfield offset   ; [value, base, offset]
     *   dup2_x2           ; [base, offset, value, base, offset]
     *   pop2              ; [base, offset, value]
     *   Xastore           ; []
     */
    if (is_wide)
    {
        codebuilder_build_dup2_x1(cg->builder);
        codebuilder_build_pop2(cg->builder);
    }
    else
    {
        codebuilder_build_swap(cg->builder);
    }
    codebuilder_build_dup(cg->builder);
    codebuilder_build_getfield(cg->builder, base_field);
    codebuilder_build_dup_x1(cg->builder);
    codebuilder_build_pop(cg->builder);
    codebuilder_build_getfield(cg->builder, offset_field);
    if (is_wide)
    {
        codebuilder_build_dup2_x2(cg->builder);
    }
    else
    {
        codebuilder_build_dup2_x1(cg->builder);
    }
    codebuilder_build_pop2(cg->builder);
    cg_emit_astore_for_type(cg->builder, type_idx);
}

void cg_emit_ptr_subscript(CodegenVisitor *cg, TypeSpecifier *ptr_type)
{
    PtrTypeIndex type_idx = cg_ptr_type_index(ptr_type);
    ptr_usage_mark(type_idx);

    const char *class_name = ptr_type_class_name(type_idx);
    const char *base_desc = ptr_type_base_descriptor(type_idx);
    ConstantPoolBuilder *cp = code_output_cp(cg->output);
    int base_field = cp_builder_add_fieldref(cp, class_name, "base", base_desc);
    int offset_field = cp_builder_add_fieldref(cp, class_name, "offset", "I");

    /* Stack: [ptr, index]
     * Result: [ptr.base[ptr.offset + index]]
     *
     *   swap              ; [index, ptr]
     *   dup               ; [index, ptr, ptr]
     *   getfield base     ; [index, ptr, base]
     *   dup_x2            ; [base, index, ptr, base]
     *   pop               ; [base, index, ptr]
     *   getfield offset   ; [base, index, offset]
     *   iadd              ; [base, index+offset]
     *   Xaload            ; [element]
     */
    codebuilder_build_swap(cg->builder);
    codebuilder_build_dup(cg->builder);
    codebuilder_build_getfield(cg->builder, base_field);
    codebuilder_build_dup_x2(cg->builder);
    codebuilder_build_pop(cg->builder);
    codebuilder_build_getfield(cg->builder, offset_field);
    codebuilder_build_iadd(cg->builder);
    cg_emit_aload_for_type(cg->builder, type_idx);

    /* For unsigned char (uint8_t), mask with 0xFF to convert signed byte to unsigned */
    if (type_idx == PTR_TYPE_CHAR)
    {
        TypeSpecifier *elem_type = cs_type_child(ptr_type);
        if (elem_type && cs_type_is_unsigned(elem_type))
        {
            codebuilder_build_iconst(cg->builder, 255);
            codebuilder_build_iand(cg->builder);
        }
    }
}

void cg_emit_ptr_store_subscript(CodegenVisitor *cg, TypeSpecifier *ptr_type)
{
    PtrTypeIndex type_idx = cg_ptr_type_index(ptr_type);
    ptr_usage_mark(type_idx);

    const char *class_name = ptr_type_class_name(type_idx);
    const char *base_desc = ptr_type_base_descriptor(type_idx);
    ConstantPoolBuilder *cp = code_output_cp(cg->output);
    int base_field = cp_builder_add_fieldref(cp, class_name, "base", base_desc);
    int offset_field = cp_builder_add_fieldref(cp, class_name, "offset", "I");
    bool is_wide = ptr_type_is_wide(type_idx);

    /* Stack: [ptr, index, value]
     * Result: [] (ptr.base[ptr.offset + index] = value)
     *
     * For category 1:
     *   dup_x2            ; [value, ptr, index, value]
     *   pop               ; [value, ptr, index]
     *   swap              ; [value, index, ptr]
     *   dup               ; [value, index, ptr, ptr]
     *   getfield base     ; [value, index, ptr, base]
     *   dup_x2            ; [value, base, index, ptr, base]
     *   pop               ; [value, base, index, ptr]
     *   getfield offset   ; [value, base, index, offset]
     *   iadd              ; [value, base, index+offset]
     *   dup2_x1           ; [base, index+offset, value, base, index+offset]
     *   pop2              ; [base, index+offset, value]
     *   Xastore           ; []
     *
     * For category 2:
     *   dup2_x2           ; [value, ptr, index, value]
     *   pop2              ; [value, ptr, index]
     *   swap              ; [value, index, ptr]
     *   dup               ; [value, index, ptr, ptr]
     *   getfield base     ; [value, index, ptr, base]
     *   dup_x2            ; [value, base, index, ptr, base]
     *   pop               ; [value, base, index, ptr]
     *   getfield offset   ; [value, base, index, offset]
     *   iadd              ; [value, base, index+offset]
     *   dup2_x2           ; [base, index+offset, value, base, index+offset]
     *   pop2              ; [base, index+offset, value]
     *   Xastore           ; []
     */
    if (is_wide)
    {
        codebuilder_build_dup2_x2(cg->builder);
    }
    else
    {
        codebuilder_build_dup_x2(cg->builder);
    }
    codebuilder_build_pop(cg->builder);
    codebuilder_build_swap(cg->builder);
    codebuilder_build_dup(cg->builder);
    codebuilder_build_getfield(cg->builder, base_field);
    codebuilder_build_dup_x2(cg->builder);
    codebuilder_build_pop(cg->builder);
    codebuilder_build_getfield(cg->builder, offset_field);
    codebuilder_build_iadd(cg->builder);
    if (is_wide)
    {
        codebuilder_build_dup2_x2(cg->builder);
    }
    else
    {
        codebuilder_build_dup2_x1(cg->builder);
    }
    codebuilder_build_pop2(cg->builder);
    cg_emit_astore_for_type(cg->builder, type_idx);
}

void cg_emit_ptr_get_base(CodegenVisitor *cg, TypeSpecifier *ptr_type)
{
    PtrTypeIndex type_idx = cg_ptr_type_index(ptr_type);
    const char *class_name = ptr_type_class_name(type_idx);
    const char *base_desc = ptr_type_base_descriptor(type_idx);

    int base_field = cp_builder_add_fieldref(code_output_cp(cg->output),
                                             class_name, "base", base_desc);
    codebuilder_build_getfield(cg->builder, base_field);
}

void cg_emit_ptr_get_offset(CodegenVisitor *cg, TypeSpecifier *ptr_type)
{
    PtrTypeIndex type_idx = cg_ptr_type_index(ptr_type);
    const char *class_name = ptr_type_class_name(type_idx);

    int offset_field = cp_builder_add_fieldref(code_output_cp(cg->output),
                                               class_name, "offset", "I");
    codebuilder_build_getfield(cg->builder, offset_field);
}

void cg_emit_ptr_clone(CodegenVisitor *cg, TypeSpecifier *ptr_type)
{
    PtrTypeIndex type_idx = cg_ptr_type_index(ptr_type);
    ptr_usage_mark(type_idx);

    const char *class_name = ptr_type_class_name(type_idx);
    const char *base_desc = ptr_type_base_descriptor(type_idx);
    ConstantPoolBuilder *cp = code_output_cp(cg->output);

    int class_idx = cp_builder_add_class(cp, class_name);
    int init_idx = cp_builder_add_methodref(cp, class_name, "<init>", "()V");
    int base_field = cp_builder_add_fieldref(cp, class_name, "base", base_desc);
    int offset_field = cp_builder_add_fieldref(cp, class_name, "offset", "I");

    /* Stack: [src_ptr]
     * Result: [new_ptr] with new_ptr.base = src_ptr.base, new_ptr.offset = src_ptr.offset
     *
     *   dup               ; [src_ptr, src_ptr]
     *   getfield base     ; [src_ptr, base]
     *   swap              ; [base, src_ptr]
     *   getfield offset   ; [base, offset]
     *   (then create new ptr like ptr_add does)
     */
    codebuilder_build_dup(cg->builder);
    codebuilder_build_getfield(cg->builder, base_field);
    codebuilder_build_swap(cg->builder);
    codebuilder_build_getfield(cg->builder, offset_field);

    /* Now stack is [base, offset], create new ptr using high-level builders */
    codebuilder_build_new(cg->builder, class_idx);
    codebuilder_build_dup(cg->builder);
    codebuilder_build_invokespecial(cg->builder, init_idx);
    codebuilder_build_dup_x2(cg->builder);
    codebuilder_build_swap(cg->builder);
    codebuilder_build_putfield(cg->builder, offset_field);
    codebuilder_build_swap(cg->builder);
    codebuilder_build_dup_x1(cg->builder);
    codebuilder_build_swap(cg->builder);
    codebuilder_build_putfield(cg->builder, base_field);
}
