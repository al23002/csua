/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison interface for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

#ifndef YY_YY_PARSER_H_INCLUDED
# define YY_YY_PARSER_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif
/* "%code requires" blocks.  */
#line 194 "parser.y"

typedef struct DeclaratorInfo_tag
{
    char *name;
    ParsedType *type;
    ParameterList *parameters;
    AttributeSpecifier *attributes;
    bool is_function; /* true if declarator has () */
} DeclaratorInfo;

typedef struct DeclaratorInfoNode_tag
{
    DeclaratorInfo info;
    struct DeclaratorInfoNode_tag *next;
} DeclaratorInfoNode;

/* YYLTYPE is defined in scanner.h */
#define YYLTYPE_IS_DECLARED

#ifndef YYLLOC_DEFAULT
#define YYLLOC_DEFAULT(Current, Rhs, N)                                      \
    do                                                                       \
    {                                                                        \
        if (N)                                                               \
        {                                                                    \
            (Current).first_line = YYRHSLOC(Rhs, 1).first_line;               \
            (Current).first_column = YYRHSLOC(Rhs, 1).first_column;           \
            (Current).last_line = YYRHSLOC(Rhs, N).last_line;                 \
            (Current).last_column = YYRHSLOC(Rhs, N).last_column;             \
            (Current).filename = YYRHSLOC(Rhs, N).filename;                   \
        }                                                                    \
        else                                                                 \
        {                                                                    \
            (Current).first_line = (Current).last_line = YYRHSLOC(Rhs, 0).last_line; \
            (Current).first_column = (Current).last_column = YYRHSLOC(Rhs, 0).last_column; \
            (Current).filename = YYRHSLOC(Rhs, 0).filename;                   \
        }                                                                    \
    } while (0)
#endif

#line 90 "parser.h"

/* Token kinds.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    YYEMPTY = -2,
    YYEOF = 0,                     /* "end of file"  */
    YYerror = 256,                 /* error  */
    YYUNDEF = 257,                 /* "invalid token"  */
    LP = 258,                      /* LP  */
    RP = 259,                      /* RP  */
    LC = 260,                      /* LC  */
    RC = 261,                      /* RC  */
    COMMA = 262,                   /* COMMA  */
    LBRACKET = 263,                /* LBRACKET  */
    RBRACKET = 264,                /* RBRACKET  */
    ATTRIBUTE = 265,               /* ATTRIBUTE  */
    LOGICAL_AND = 266,             /* LOGICAL_AND  */
    LOGICAL_OR = 267,              /* LOGICAL_OR  */
    BIT_AND = 268,                 /* BIT_AND  */
    BIT_OR = 269,                  /* BIT_OR  */
    BIT_XOR = 270,                 /* BIT_XOR  */
    EQ = 271,                      /* EQ  */
    ASSIGN_T = 272,                /* ASSIGN_T  */
    NE = 273,                      /* NE  */
    GT = 274,                      /* GT  */
    GE = 275,                      /* GE  */
    LE = 276,                      /* LE  */
    LT = 277,                      /* LT  */
    SEMICOLON = 278,               /* SEMICOLON  */
    COLON = 279,                   /* COLON  */
    QUESTION = 280,                /* QUESTION  */
    ADD = 281,                     /* ADD  */
    SUB = 282,                     /* SUB  */
    MUL = 283,                     /* MUL  */
    DIV = 284,                     /* DIV  */
    MOD = 285,                     /* MOD  */
    ADD_ASSIGN_T = 286,            /* ADD_ASSIGN_T  */
    SUB_ASSIGN_T = 287,            /* SUB_ASSIGN_T  */
    MUL_ASSIGN_T = 288,            /* MUL_ASSIGN_T  */
    DIV_ASSIGN_T = 289,            /* DIV_ASSIGN_T  */
    MOD_ASSIGN_T = 290,            /* MOD_ASSIGN_T  */
    INCREMENT = 291,               /* INCREMENT  */
    DECREMENT = 292,               /* DECREMENT  */
    EXCLAMATION = 293,             /* EXCLAMATION  */
    DOT = 294,                     /* DOT  */
    ARROW = 295,                   /* ARROW  */
    LSHIFT = 296,                  /* LSHIFT  */
    RSHIFT = 297,                  /* RSHIFT  */
    TILDE = 298,                   /* TILDE  */
    ELLIPSIS = 299,                /* ELLIPSIS  */
    AND_ASSIGN_T = 300,            /* AND_ASSIGN_T  */
    OR_ASSIGN_T = 301,             /* OR_ASSIGN_T  */
    XOR_ASSIGN_T = 302,            /* XOR_ASSIGN_T  */
    LSHIFT_ASSIGN_T = 303,         /* LSHIFT_ASSIGN_T  */
    RSHIFT_ASSIGN_T = 304,         /* RSHIFT_ASSIGN_T  */
    INT_LITERAL = 305,             /* INT_LITERAL  */
    UINT_LITERAL = 306,            /* UINT_LITERAL  */
    LONG_LITERAL = 307,            /* LONG_LITERAL  */
    ULONG_LITERAL = 308,           /* ULONG_LITERAL  */
    DOUBLE_LITERAL = 309,          /* DOUBLE_LITERAL  */
    FLOAT_LITERAL = 310,           /* FLOAT_LITERAL  */
    IDENTIFIER = 311,              /* IDENTIFIER  */
    STRING_LITERAL = 312,          /* STRING_LITERAL  */
    IF = 313,                      /* IF  */
    ELSE = 314,                    /* ELSE  */
    ELSIF = 315,                   /* ELSIF  */
    WHILE = 316,                   /* WHILE  */
    DO = 317,                      /* DO  */
    FOR = 318,                     /* FOR  */
    RETURN = 319,                  /* RETURN  */
    BREAK = 320,                   /* BREAK  */
    CONTINUE = 321,                /* CONTINUE  */
    INT_T = 322,                   /* INT_T  */
    DOUBLE_T = 323,                /* DOUBLE_T  */
    STRING_T = 324,                /* STRING_T  */
    VOID_T = 325,                  /* VOID_T  */
    CHAR_T = 326,                  /* CHAR_T  */
    BOOL_T = 327,                  /* BOOL_T  */
    SHORT_T = 328,                 /* SHORT_T  */
    LONG_T = 329,                  /* LONG_T  */
    UNSIGNED_T = 330,              /* UNSIGNED_T  */
    FLOAT_T = 331,                 /* FLOAT_T  */
    TRUE_T = 332,                  /* TRUE_T  */
    FALSE_T = 333,                 /* FALSE_T  */
    NULL_T = 334,                  /* NULL_T  */
    STATIC_T = 335,                /* STATIC_T  */
    CONST_T = 336,                 /* CONST_T  */
    EXTERN_T = 337,                /* EXTERN_T  */
    TYPEDEF_T = 338,               /* TYPEDEF_T  */
    STRUCT_T = 339,                /* STRUCT_T  */
    UNION_T = 340,                 /* UNION_T  */
    ENUM_T = 341,                  /* ENUM_T  */
    SWITCH = 342,                  /* SWITCH  */
    CASE = 343,                    /* CASE  */
    DEFAULT = 344,                 /* DEFAULT  */
    GOTO = 345,                    /* GOTO  */
    SIZEOF = 346,                  /* SIZEOF  */
    LOWER_THAN_ELSE = 347          /* LOWER_THAN_ELSE  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 235 "parser.y"

    int                  iv;
    long                 lv;
    double               dv;
    float                fv;
    char                *name;
    CS_String            str;
    Expression          *expression;
    ExpressionList      *expression_list;
    Statement           *statement;
    StatementList       *statement_list;
    FunctionDeclaration *function_declaration;
    AssignmentOperator   assignment_operator;
    TypeSpecifier       *type_specifier;
    ParsedType          *parsed_type;
    ParameterList       *parameter_list;
    ArgumentList        *argument_list;
    AttributeSpecifier  *attribute;
    DeclaratorInfo       declarator;
    StructMember        *struct_member;
    EnumMember          *enum_member;
    DeclaratorInfoNode *declarator_list;

#line 223 "parser.h"

};
typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif

/* Location type.  */
#if ! defined YYLTYPE && ! defined YYLTYPE_IS_DECLARED
typedef struct YYLTYPE YYLTYPE;
struct YYLTYPE
{
  int first_line;
  int first_column;
  int last_line;
  int last_column;
};
# define YYLTYPE_IS_DECLARED 1
# define YYLTYPE_IS_TRIVIAL 1
#endif




int yyparse (Scanner *scanner);


#endif /* !YY_YY_PARSER_H_INCLUDED  */
