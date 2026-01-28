/* codebuilder_internal.h - Internal functions aggregation header
 *
 * This header redirects to the actual implementation headers for Cminor compatibility.
 * Each function is declared in and implemented by its corresponding module:
 * - codebuilder_frame.h/.c: stack/frame operations
 * - codebuilder_types.h/.c: type descriptor operations
 * - codebuilder_label.h/.c: label operations
 * - codebuilder_control.h/.c: control flow operations
 */
#pragma once

#include "codebuilder_frame.h"
#include "codebuilder_types.h"
#include "codebuilder_label.h"
#include "codebuilder_control.h"
