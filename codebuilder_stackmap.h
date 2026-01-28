#pragma once

#include "codebuilder_defs.h"

CF_StackMapFrame *codebuilder_generate_stackmap(CodeBuilder *builder,
                                                CF_ConstantPool *cp,
                                                int *frame_count);
void codebuilder_free_stackmap(CF_StackMapFrame *frames, int count);
