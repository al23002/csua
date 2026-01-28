#pragma once

#include "ast.h"
#include "code_output.h"
#include "codebuilder_defs.h"
#include "compiler.h"
#include "executable.h"
#include "stackmap.h"
#include "visitor.h"

/* Forward declarations */
typedef struct CodegenVisitor_tag CodegenVisitor;
typedef struct CodegenIfContext_tag CodegenIfContext;
typedef struct CodegenForContext_tag CodegenForContext;
typedef struct CodegenSwitchContext_tag CodegenSwitchContext;
typedef struct CodegenSymbol_tag CodegenSymbol;

/* Note: Local variable slot management is delegated to CodeBuilder
 * using codebuilder_begin_block/end_block for block-level scoping.
 * Symbol mappings (Declaration -> slot) persist for the entire function.
 * scope_depth tracks block nesting depth (paired with CodeBuilder's block_depth). */

/* Context structure for managing code generation state
 * Note: Control flow (break/continue targets) is managed by CodeBuilder's
 * control_stack. Use codebuilder_emit_break/continue(). */
typedef struct CodegenContext_tag
{
    int scope_depth; /* Block nesting depth (for underflow checks) */

    CodegenSymbol *symbol_stack; /* Declaration -> slot mapping (persists for entire function) */

    CodegenIfContext *if_stack;
    int if_depth;
    int if_capacity;

    CodegenForContext *for_stack;
    int for_depth;
    int for_capacity;

    CodegenSwitchContext *switch_stack;
    int switch_depth;
    int switch_capacity;

    /* Note: Local slot management (current_locals, max_locals) is handled
     * by CodeBuilder (Javac-style). See builder.frame.locals_count and
     * builder.max_locals. */

    bool has_return;

    struct Expression_tag *assign_target;
    bool assign_is_simple;
    struct Expression_tag *addr_target; /* Target of ADDRESS_EXPRESSION (&) */
    struct Expression_tag *inc_target;  /* Target of INCREMENT/DECREMENT */
    int flatten_init_depth;

    /* Label registry for goto/label support (function-scoped) */
    char **label_names;       /* Array of label names */
    CB_Label **label_targets; /* Corresponding CB_Label pointers */
    int label_count;
    int label_capacity;
} CodegenContext;

/* If statement context */
struct CodegenIfContext_tag
{
    Statement *if_stmt;
    Statement *then_stmt;
    Statement *else_stmt;
    CB_Label *then_block;
    CB_Label *else_block;
    CB_Label *end_block;
    bool has_cond_branch;
    bool then_alive; /* Was code alive at end of then block */
    bool else_alive; /* Was code alive at end of else block */
};

/* For loop context
 * Labels are managed by CodeBuilder's CB_LoopContext.
 * Access via codebuilder_current_loop(). */
struct CodegenForContext_tag
{
    Statement *for_stmt;
    Statement *body_stmt;
    Expression *condition_expr;
    Expression *post_expr;
    bool is_do_while;
    bool has_cond_branch;
    bool body_alive; /* Was code alive at end of loop body */
};

/* Switch statement context
 * Labels and cases are managed by CodeBuilder's CB_SwitchContext.
 * Access via codebuilder_current_switch(). */
struct CodegenSwitchContext_tag
{
    Statement *switch_stmt;
    Statement *body_stmt;
    Expression *expression;
    CF_ValueTag expr_tag; /* Type of switch expression (AST-derived) */
    bool has_expr_local;
    bool has_dispatch_goto;
    bool any_case_alive; /* Was any case block alive at its end */
};

/* Main CodegenVisitor structure */
struct CodegenVisitor_tag
{
    Visitor visitor;
    CS_Compiler *compiler;
    CS_Executable *exec;

    FunctionDeclaration *current_function;

    /* Bytecode and constant pool output (shared with CodeBuilder) */
    CodeOutput *output;

    CG_StaticField *static_fields;
    int static_field_count;
    int static_field_capacity;

    CG_ClassDef *class_defs;
    int class_def_count;
    int class_def_capacity;

    CS_Function *functions;
    int function_count;
    int function_capacity;

    BytecodeInstr *bytecode;
    int bytecode_count;
    int bytecode_capacity;
    int last_bytecode_index;
    bool has_last_bytecode;

    CodegenContext ctx;
    CodeBuilder *builder;
    const char *current_class_name; /* For StackMap object types */
    CF_ConstantPool *stackmap_cp;   /* Constant pool used while building StackMapTable */

    /* Temporary storage for generated StackMapTable frames */
    CF_StackMapFrame *temp_stack_map_frames;
    int temp_stack_map_frame_count;
};

CodegenVisitor *create_codegen_visitor(CS_Compiler *compiler,
                                       CS_Executable *exec,
                                       const char *class_name);
void codegen_begin_function(CodegenVisitor *v, FunctionDeclaration *func);
void codegen_finish_function(CodegenVisitor *v);
void cg_record_bytecode(CodegenVisitor *v, CF_Opcode opcode, int pc, int length);

/* Switch-based AST traversal (replaces function pointer dispatch) */
void codegen_traverse_expr(struct Expression_tag *expr, CodegenVisitor *cg);
void codegen_traverse_stmt(struct Statement_tag *stmt, CodegenVisitor *cg);
