#pragma once

#include "cminor_base.h"

/*
 * HeaderDeclVisitor - Processes function declarations after parsing
 *
 * This visitor iterates FileDecl->functions, resolves types for prototypes,
 * and registers them in DeclarationRegistry.
 */

/*
 * Process function declarations after parsing.
 *
 * - Resolves types for prototypes (body == NULL)
 * - Registers prototypes to DeclarationRegistry
 *
 * @param tu The translation unit
 * @param source_path Path of the source file being processed
 */
void header_decl_visitor_process(CS_Compiler *compiler, const char *source_path);

