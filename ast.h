#pragma once

/*
 * ast.h - Abstract Syntax Tree definitions
 *
 * Contains:
 * - Expression and Statement definitions
 * - Declaration and FunctionDeclaration
 * - Supporting types (ParameterList, ArgumentList, etc.)
 * - List types (ExpressionList, StatementList, etc.)
 */

#include "cminor_base.h"
#include "type_specifier.h"
#include "parsed_type.h"

/* ============================================================
 * Declaration Types
 * ============================================================ */

typedef struct Declaration_tag
{
    char *name;
    TypeSpecifier *type;
    ParsedType *parsed_type;
    Expression *initializer;
    char *class_name;     /* Owning Java class derived from source path */
    char *source_path;    /* Path of the translation unit where this declaration appears */
    int index;            /* Set during code generation (0 during parsing) */
    bool needs_heap_lift; /* True if address is taken (&var) - variable must be boxed on heap */
    bool is_static;       /* static variable -> private in JVM */
    bool is_extern;       /* extern declaration -> no field generation, just reference */
} Declaration;

typedef struct ParameterList_tag
{
    TypeSpecifier *type;
    ParsedType *parsed_type;
    char *name;
    int line_number;
    bool is_ellipsis;
    struct Declaration_tag *decl; /* Declaration created by meanvisitor for codegen heap-lift */
    struct ParameterList_tag *next;
} ParameterList;

typedef enum
{
    CS_ATTRIBUTE_UNKNOWN,
    CS_ATTRIBUTE_DEPRECATED,
    CS_ATTRIBUTE_GET_STATIC,
    CS_ATTRIBUTE_INVOKE_VIRTUAL,
    CS_ATTRIBUTE_INVOKE_STATIC,
    CS_ATTRIBUTE_INVOKE_SPECIAL,
    CS_ATTRIBUTE_GET_FIELD,
    CS_ATTRIBUTE_NEW,
    CS_ATTRIBUTE_ARRAYLENGTH,
    CS_ATTRIBUTE_AALOAD,
    CS_ATTRIBUTE_CLINIT,
} CS_AttributeKind;

typedef struct AttributeSpecifier_tag
{
    CS_AttributeKind kind;
    char *text;
    char *class_name;  /* For get_static/invoke_virtual */
    char *member_name; /* For get_static/invoke_virtual */
    char *descriptor;  /* For get_static/invoke_virtual */
    struct AttributeSpecifier_tag *next;
} AttributeSpecifier;

typedef struct ArgumentList_tag
{
    Expression *expr;
    struct ArgumentList_tag *next;
} ArgumentList;

typedef struct FunctionDeclaration_tag
{
    char *name;
    TypeSpecifier *type;
    ParsedType *parsed_type;
    ParameterList *param;
    bool is_variadic;
    bool is_static; /* static function -> private in JVM */
    AttributeSpecifier *attributes;
    Statement *body;
    char *class_name;  /* Owning Java class derived from source path */
    char *source_path; /* Path of the translation unit where this declaration appears */
    int index;         /* Set during code generation (0 during parsing) */
    int varargs_index; /* Local variable index for __varargs array (variadic functions only) */
} FunctionDeclaration;

/* ============================================================
 * Expression Types
 * ============================================================ */

typedef enum
{
    DOUBLE_EXPRESSION = 1,
    FLOAT_EXPRESSION,
    LONG_EXPRESSION,
    ULONG_EXPRESSION,
    BOOL_EXPRESSION,
    NULL_EXPRESSION,
    INT_EXPRESSION,
    UINT_EXPRESSION,
    STRING_EXPRESSION,
    IDENTIFIER_EXPRESSION,
    INCREMENT_EXPRESSION,
    DECREMENT_EXPRESSION,
    FUNCTION_CALL_EXPRESSION,
    MINUS_EXPRESSION,
    PLUS_EXPRESSION,
    LOGICAL_NOT_EXPRESSION,
    BIT_NOT_EXPRESSION,
    ADDRESS_EXPRESSION,
    DEREFERENCE_EXPRESSION,
    SIZEOF_EXPRESSION,
    MUL_EXPRESSION,
    DIV_EXPRESSION,
    MOD_EXPRESSION,
    ADD_EXPRESSION,
    SUB_EXPRESSION,
    LSHIFT_EXPRESSION,
    RSHIFT_EXPRESSION,
    GT_EXPRESSION,
    GE_EXPRESSION,
    LT_EXPRESSION,
    LE_EXPRESSION,
    EQ_EXPRESSION,
    NE_EXPRESSION,
    BIT_AND_EXPRESSION,
    BIT_XOR_EXPRESSION,
    BIT_OR_EXPRESSION,
    LOGICAL_AND_EXPRESSION,
    LOGICAL_OR_EXPRESSION,
    ASSIGN_EXPRESSION,
    CAST_EXPRESSION,
    TYPE_CAST_EXPRESSION,
    ARRAY_EXPRESSION,
    MEMBER_EXPRESSION,
    INITIALIZER_LIST_EXPRESSION,
    DESIGNATED_INITIALIZER_EXPRESSION,
    CONDITIONAL_EXPRESSION,
    COMMA_EXPRESSION,
    ARRAY_TO_POINTER_EXPRESSION,
    EXPRESSION_KIND_PLUS_ONE
} ExpressionKind;

typedef struct
{
    Expression *function;
    ArgumentList *argument;
} FunctionCallExpression;

typedef struct
{
    Expression *array;
    Expression *index;
} ArrayExpression;

typedef struct
{
    Expression *target;
    char *member_name;
    bool via_pointer;
} MemberExpression;

typedef struct
{
    char *name;
    bool is_function;
    bool is_enum_member;
    union
    {
        Declaration *declaration;
        FunctionDeclaration *function;
        EnumMember *enum_member;
    } u;
} IdentifierExpression;

typedef struct
{
    Expression *left;
    Expression *right;
} BinaryExpression;

typedef enum
{
    ASSIGN = 1,
    ADD_ASSIGN,
    SUB_ASSIGN,
    MUL_ASSIGN,
    DIV_ASSIGN,
    MOD_ASSIGN,
    AND_ASSIGN,
    OR_ASSIGN,
    XOR_ASSIGN,
    LSHIFT_ASSIGN,
    RSHIFT_ASSIGN,
    ASSIGN_PLUS_ONE
} AssignmentOperator;

typedef struct
{
    CS_CastType ctype;
    Expression *expr;
} CastExpression;

typedef struct
{
    TypeSpecifier *type;
    Expression *expr;
} TypeCastExpression;

typedef struct
{
    TypeSpecifier *type;
    Expression *expr;
    bool is_type;
    int computed_value; /* For sizeof identifier: array element count or 1 */
} SizeofExpression;

typedef struct
{
    AssignmentOperator aope;
    Expression *left;
    Expression *right;
} AssignmentExpression;

typedef struct
{
    Expression *condition;
    Expression *true_expr;
    Expression *false_expr;
} ConditionalExpression;

typedef struct
{
    Expression *left;
    Expression *right;
} CommaExpression;

typedef struct
{
    char *field_name;
    Expression *value;
} DesignatedInitializerExpression;

typedef struct
{
    const char *path;
    int line;
} CS_InputLocation;

struct Expression_tag
{
    ExpressionKind kind;
    TypeSpecifier *type;
    ParsedType *parsed_type;
    CS_InputLocation input_location;
    int line_number;
    union
    {
        double double_value;
        float float_value;
        long long_value;
        bool bool_value;
        int int_value;
        CS_String string_value;
        IdentifierExpression identifier;
        struct
        {
            Expression *target;
            bool is_prefix;
        } inc_dec;
        FunctionCallExpression function_call_expression;
        Expression *minus_expression;
        Expression *plus_expression;
        Expression *logical_not_expression;
        Expression *bit_not_expression;
        Expression *address_expression;
        Expression *dereference_expression;
        BinaryExpression binary_expression;
        AssignmentExpression assignment_expression;
        CastExpression cast_expression;
        TypeCastExpression type_cast_expression;
        SizeofExpression sizeof_expression;
        ArrayExpression array_expression;
        MemberExpression member_expression;
        ExpressionList *initializer_list;
        DesignatedInitializerExpression designated_initializer;
        ConditionalExpression conditional_expression;
        CommaExpression comma_expression;
        Expression *array_to_pointer;
    } u;
};

/* ============================================================
 * Statement Types
 * ============================================================ */

typedef enum
{
    EXPRESSION_STATEMENT = 1,
    DECLARATION_STATEMENT,
    COMPOUND_STATEMENT,
    IF_STATEMENT,
    WHILE_STATEMENT,
    DO_WHILE_STATEMENT,
    FOR_STATEMENT,
    SWITCH_STATEMENT,
    CASE_STATEMENT,
    DEFAULT_STATEMENT,
    GOTO_STATEMENT,
    LABEL_STATEMENT,
    BREAK_STATEMENT,
    CONTINUE_STATEMENT,
    RETURN_STATEMENT,
    STATEMENT_TYPE_COUNT_PLUS_ONE
} StatementType;

struct Statement_tag
{
    StatementType type;
    int line_number;
    union
    {
        Expression *expression_s;
        Declaration *declaration_s;
        struct
        {
            StatementList *list;
        } compound_s;
        struct
        {
            Expression *condition;
            Statement *then_statement;
            Statement *else_statement;
        } if_s;
        struct
        {
            Expression *condition;
            Statement *body;
        } while_s;
        struct
        {
            Expression *condition;
            Statement *body;
        } do_s;
        struct
        {
            Statement *init;
            Expression *condition;
            Expression *post;
            Statement *body;
        } for_s;
        struct
        {
            Expression *expression;
            Statement *body;
        } switch_s;
        struct
        {
            Expression *expression;
            Statement *statement;
        } case_s;
        struct
        {
            Statement *statement;
        } default_s;
        struct
        {
            char *label;
        } goto_s;
        struct
        {
            char *label;
            Statement *statement;
        } label_s;
        struct
        {
            Expression *expression;
        } return_s;
    } u;
};

/* ============================================================
 * List Types
 * ============================================================ */

typedef struct ExpressionList_tag
{
    Expression *expression;
    struct ExpressionList_tag *next;
} ExpressionList;

typedef struct StatementList_tag
{
    Statement *stmt;
    struct StatementList_tag *next;
} StatementList;

typedef struct DeclarationList_tag
{
    Declaration *decl;
    struct DeclarationList_tag *next;
} DeclarationList;

typedef struct FunctionDeclarationList_tag
{
    FunctionDeclaration *func;
    struct FunctionDeclarationList_tag *next;
} FunctionDeclarationList;
