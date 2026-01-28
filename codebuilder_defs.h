#pragma once

#include "ast.h"
#include "classfile.h"
#include "classfile_opcode.h"
#include "constant_pool.h"
#include "method_code.h"

/* Maximum locals and stack size for tracking */
enum
{
    CB_MAX_LOCALS = 4096,
    CB_MAX_STACK = 4096,
    CB_MAX_SCOPE_DEPTH = 64
};

typedef struct CodegenVisitor_tag CodegenVisitor;

/*
 * Verification Type - JVM type for stack/locals tracking
 * Used for StackMapTable generation
 */
typedef struct CB_VerificationType_tag
{
    CF_VerificationTypeTag tag;
    union
    {
        /* For OBJECT: class name (internal format, e.g., "Ljava/lang/String;") */
        const char *class_name;
        /* For UNINITIALIZED: bytecode offset of new instruction */
        int offset;
    } u;
} CB_VerificationType;

/*
 * Frame State - complete type state at a bytecode offset
 * Tracks all locals and stack types at a specific program point
 */
typedef struct CB_Frame_tag
{
    /* Local variable types (dynamically allocated) */
    CB_VerificationType *locals;
    int locals_count;

    /* Operand stack types (dynamically allocated) */
    CB_VerificationType *stack;
    int stack_count;
} CB_Frame;

/*
 * Branch Target - snapshot of frame state at a branch target
 * Used for StackMapTable generation
 */
typedef struct CB_BranchTarget_tag
{
    int pc;            /* Target bytecode offset */
    CB_Frame *frame;   /* Frame state at this target (owned) */
    bool is_exception; /* True if this is an exception handler entry */
} CB_BranchTarget;

/*
 * Jump Source - diagnostic info about where a jump originates
 */
typedef struct CB_JumpSource_tag
{
    int pc;              /* PC of the jump instruction */
    int line;            /* Source line number (0 if unknown) */
    CB_Frame *frame;     /* Frame state at jump (owned copy) */
    const char *context; /* "goto", "break", "continue", "if", "loop", etc. */
} CB_JumpSource;

/*
 * Label - lightweight jump target (internal use)
 * Replaces the old CB_BasicBlock with simpler structure
 *
 * Frame state lifecycle for jump-only labels:
 * 1. Mark label as jump_only via codebuilder_mark_jump_only()
 * 2. Jump to label -> frame captured
 * 3. Place label -> frame automatically restored
 * This eliminates manual frame restoration for labels only reached by jumps.
 */
typedef struct CB_Label_tag
{
    int id;              /* Label identifier */
    int pc;              /* PC when placed (-1 if unresolved) */
    CB_Frame *frame;     /* Frame state at label (owned) */
    bool is_placed;      /* Label has been positioned in code */
    bool is_loop_header; /* Label is a backward branch target */
    bool frame_recorded; /* Frame has been recorded for StackMap */
    bool frame_saved;    /* Frame was saved by a jump */
    bool jump_only;      /* Label is only reached by jumps (auto-restore enabled) */
    bool is_jump_target; /* Label is target of at least one jump instruction */

    /* Diagnostic: all jump sources to this label */
    const char *name; /* Label name for diagnostics (e.g., "L1", "loop_end") */
    CB_JumpSource *jump_sources;
    int jump_source_count;
    int jump_source_capacity;
} CB_Label;

/*
 * Pending Jump - jump instruction waiting for target resolution
 */
typedef struct CB_PendingJump_tag
{
    int jump_pc;      /* PC of the jump instruction */
    CB_Label *target; /* Target label */
} CB_PendingJump;

/*
 * Control Flow Context Types
 */
typedef enum CB_ControlKind_tag
{
    CB_CONTROL_IF,
    CB_CONTROL_LOOP,
    CB_CONTROL_SWITCH,
} CB_ControlKind;

/*
 * If Context - tracks if/else/endif structure
 */
typedef struct CB_IfContext_tag
{
    CB_Label *then_label; /* Start of then block */
    CB_Label *else_label; /* Start of else block (NULL if no else) */
    CB_Label *end_label;  /* End of if statement */
    bool has_else;        /* True if else block exists */
    bool in_then;         /* Currently generating then block */
    bool in_else;         /* Currently generating else block */
    bool then_alive;      /* Was code alive at end of then block */
    bool else_alive;      /* Was code alive at end of else block */
} CB_IfContext;

/*
 * Loop Context - tracks while/for/do-while structure
 */
typedef struct CB_LoopContext_tag
{
    CB_Label *start_label;    /* Loop start (for backward jumps) */
    CB_Label *cond_label;     /* Condition check */
    CB_Label *body_label;     /* Body start */
    CB_Label *post_label;     /* Post expression (for only) */
    CB_Label *end_label;      /* Loop end (break target) */
    CB_Label *continue_label; /* Continue target (cond or post) */
    bool is_do_while;         /* True for do-while loop */
    bool has_post;            /* True if has post expression (for) */
    bool body_alive;          /* Was code alive at end of body */
} CB_LoopContext;

/*
 * Switch Case Entry
 */
typedef struct CB_SwitchCase_tag
{
    int value;       /* Case value */
    CB_Label *label; /* Case label */
} CB_SwitchCase;

/*
 * Switch Context - tracks switch statement
 */
typedef struct CB_SwitchContext_tag
{
    CB_Label *dispatch_label; /* Dispatch table location */
    CB_Label *default_label;  /* Default case label */
    CB_Label *end_label;      /* Switch end (break target) */
    CB_SwitchCase *cases;     /* Case entries */
    int case_count;
    int case_capacity;
    int expr_local; /* Local var storing switch expression */
    bool has_default;
    CB_Frame *entry_frame; /* Frame state at switch entry (for case labels) */
    bool any_case_alive;   /* Was any case block alive at its end */
} CB_SwitchContext;

/*
 * Control Stack Entry - unified control flow tracking
 */
typedef struct CB_ControlEntry_tag
{
    CB_ControlKind kind;
    union
    {
        CB_IfContext if_ctx;
        CB_LoopContext loop_ctx;
        CB_SwitchContext switch_ctx;
    } u;
} CB_ControlEntry;

/*
 * CodeBuilder - bytecode generation with integrated type tracking
 *
 * Can be used in two modes:
 * 1. With CodegenVisitor: cp and method come from visitor->output
 * 2. Standalone: cp and method are provided directly
 *
 * CodeBuilder takes ConstantPoolBuilder and MethodCode directly,
 * providing clearer responsibility separation.
 */
typedef struct CodeBuilder_tag
{
    ConstantPoolBuilder *cp; /* Required: constant pool (external, not owned) */
    MethodCode *method;      /* Required: method code output (external, not owned) */

    /* Current frame state (modified as code is generated) */
    CB_Frame *frame;

    /* Initial frame state (from method signature) */
    CB_Frame *initial_frame;

    /* Branch targets requiring StackMapTable entries */
    CB_BranchTarget *branch_targets;
    int branch_target_count;
    int branch_target_capacity;

    /* Labels for jump targets (array of pointers to individually allocated labels) */
    CB_Label **labels;
    int label_count;
    int label_capacity;

    /* Pending jumps to resolve */
    CB_PendingJump *pending_jumps;
    int pending_jump_count;
    int pending_jump_capacity;

    /* Control flow context stack */
    CB_ControlEntry *control_stack;
    int control_depth;
    int control_capacity;

    /* Max stack and locals tracking */
    int max_stack;
    int max_locals;

    /* Whether codebuilder is alive (reachable) */
    bool alive;

    /* Block scope tracking (Javac-style)
     * Each block saves locals_count at entry for restoration at exit */
    int *block_locals_base;
    int block_depth;

    /* Method/class name for error messages */
    const char *method_name;
    const char *class_name;

    /* Jump context for diagnostics (set before jump, auto-cleared after) */
    const char *jump_context;

    /* Diagnostics */
    int diag_stack_underflow_count;
    int diag_stack_mismatch_count;
    int diag_dead_code_op_count;
} CodeBuilder;

typedef struct CodebuilderStackMark_tag
{
    CB_Frame *frame; /* Full frame snapshot for restoration (owned) */
    int stack_depth; /* For compatibility */
} CodebuilderStackMark;
