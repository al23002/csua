#pragma once

#include "codebuilder_defs.h"
#include "classfile.h"

/* Descriptor parsing helpers */
CB_VerificationType cb_descriptor_type(const char **p);
CB_VerificationType cb_type_from_value_tag(CF_ValueTag tag);

/* Extract element type from array type */
CB_VerificationType cb_type_array_element(CB_VerificationType array_type);

CB_VerificationType cb_type_int();
CB_VerificationType cb_type_long();
CB_VerificationType cb_type_float();
CB_VerificationType cb_type_double();
CB_VerificationType cb_type_null();
CB_VerificationType cb_type_top();
CB_VerificationType cb_type_object(const char *class_name);
CB_VerificationType cb_type_uninitialized(int offset);
CB_VerificationType cb_type_uninitialized_this();

int cb_type_slots(CB_VerificationType *type);
bool cb_type_equals(CB_VerificationType *a, CB_VerificationType *b);

/* Type compatibility check - returns true if 'value' can be assigned to 'target' */
bool cb_type_assignable(CB_VerificationType value, CB_VerificationType target);

CB_VerificationType cb_type_from_c_type(TypeSpecifier *type);

/* Type category predicates for verification */
bool cb_type_is_reference(CB_VerificationType *type);
bool cb_type_is_integer(CB_VerificationType *type);
bool cb_type_is_category1(CB_VerificationType *type);
bool cb_type_is_category2(CB_VerificationType *type);
const char *cb_type_name(CB_VerificationType *type);

void codebuilder_apply_invoke_descriptor(CodeBuilder *builder, const char *descriptor, bool has_this);
void codebuilder_apply_invoke_signature(CodeBuilder *builder, FunctionDeclaration *func, bool has_this);
