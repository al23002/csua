#pragma once

#include "cminor_base.h"
#include "visitor.h"

void leave_declstmt(Statement *stmt, Visitor *visitor);
