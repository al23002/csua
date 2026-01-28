#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ascii.h"
#include "ast.h"
#include "compiler.h"
#include "create.h"
#include "embedded_data.h"
#include "keyword.h"
#include "scanner.h"
#include "preprocessor.h"
#include "parser.h"

#define BUFFER_GROW 64

static int get_raw_char_no_continuation(Preprocessor *pp);
static int get_raw_char(Preprocessor *pp);
static void unget_raw_char(Preprocessor *pp, int ch);
static void pushback_char(Preprocessor *pp, int ch);
static void advance_logical_line(Preprocessor *pp);
static void sync_location(Preprocessor *pp);
static void mark_token_start(Preprocessor *pp);
static char *retain_string(Preprocessor *pp, const char *src);

static char *dup_string(const char *src)
{
    return strdup(src);
}

static bool register_retained_string(Preprocessor *pp, char *str)
{
    if (!pp || !str)
        return false;
    if (pp->retained_string_count == pp->retained_string_capacity)
    {
        int new_cap = pp->retained_string_capacity == 0 ? 8 : pp->retained_string_capacity * 2;
        char **new_list = (char **)calloc(new_cap, sizeof(char *));
        if (!new_list)
            return false;
        for (int i = 0; i < pp->retained_string_count; i++)
            new_list[i] = pp->retained_strings[i];
        pp->retained_strings = new_list;
        pp->retained_string_capacity = new_cap;
    }
    pp->retained_strings[pp->retained_string_count++] = str;
    return true;
}

static char *retain_string(Preprocessor *pp, const char *src)
{
    if (!src)
        return NULL;
    char *copy = dup_string(src);
    register_retained_string(pp, copy);
    return copy;
}

/* ByteBuffer helper functions */

static ByteBuffer *create_buffer(const char *data, int size, bool owned)
{
    ByteBuffer *buf = (ByteBuffer *)calloc(1, sizeof(ByteBuffer));
    if (!buf)
        return NULL;
    buf->data = data;
    buf->size = size;
    buf->position = 0;
    buf->owned = owned;
    buf->line = 1;
    buf->line_start_pos = 0;
    return buf;
}

static void free_buffer(ByteBuffer *buf)
{
    if (!buf)
        return;
    if (buf->owned)
        free((void *)buf->data);
    free(buf);
}

static int buffer_getc(ByteBuffer *buf)
{
    if (!buf || buf->position >= buf->size)
        return EOF;
    unsigned char ch = buf->data[buf->position++];
    if (ch == '\n')
    {
        buf->line++;
        buf->line_start_pos = buf->position;
    }
    return ch;
}

static void buffer_ungetc(ByteBuffer *buf)
{
    if (!buf || buf->position == 0)
        return;
    buf->position--;
    if (buf->position < buf->size && buf->data[buf->position] == '\n')
    {
        if (buf->line > 1)
            buf->line--;
        /* 前の行頭位置を再計算 */
        buf->line_start_pos = 0;
        for (int i = buf->position; i > 0; i--)
        {
            if (buf->data[i - 1] == '\n')
            {
                buf->line_start_pos = i;
                break;
            }
        }
    }
}

static int buffer_peek(ByteBuffer *buf)
{
    if (!buf || buf->position >= buf->size)
        return EOF;
    return (unsigned char)buf->data[buf->position];
}

static ByteBuffer *load_embedded(const char *name)
{
    const EmbeddedFile *file = embedded_find(name);
    if (!file)
        return NULL;

    return create_buffer((const char *)file->data, file->size, false);
}

ByteBuffer *load_from_bytes(const unsigned char *data, int size)
{
    return create_buffer((const char *)data, size, false);
}

static void init_source_stack(SourceStack *stack)
{
    stack->frames = NULL;
    stack->size = 0;
    stack->capacity = 0;
}

static void free_source_frame(SourceFrame *frame)
{
    if (frame->buffer)
    {
        free_buffer(frame->buffer);
        frame->buffer = NULL;
    }
    free(frame->dir);
    frame->path = NULL;
    frame->logical_path = NULL;
}

static void cleanup_source_stack(SourceStack *stack)
{
    for (int i = 0; i < stack->size; ++i)
    {
        free_source_frame(&stack->frames[i]);
    }
    free(stack->frames);
    stack->frames = NULL;
    stack->size = 0;
    stack->capacity = 0;
}

static char *dirname_from_path(const char *path)
{
    const char *last_slash = strrchr(path, '/');
    if (!last_slash)
        return dup_string(".");
    int len = (int)(last_slash - path);
    char *dir = (char *)calloc(len + 1, sizeof(char));
    strncpy(dir, path, len);
    dir[len] = '\0';
    return dir;
}

static bool push_source(Preprocessor *pp, const char *path, ByteBuffer *buffer)
{
    SourceStack *stack = pp->sources;
    if (stack->size == stack->capacity)
    {
        int new_cap = stack->capacity == 0 ? 4 : stack->capacity * 2;
        SourceFrame *new_frames =
            (SourceFrame *)calloc(new_cap, sizeof(SourceFrame));
        if (!new_frames)
        {
            free_buffer(buffer);
            return false;
        }
        for (int i = 0; i < stack->size; i++)
            new_frames[i] = stack->frames[i];
        stack->frames = new_frames;
        stack->capacity = new_cap;
    }
    SourceFrame *frame = &stack->frames[stack->size++];
    frame->buffer = buffer;
    frame->path = retain_string(pp, path);
    frame->logical_path = frame->path;
    frame->dir = dirname_from_path(path);
    frame->logical_line = 1;
    sync_location(pp);
    return true;
}

static void pop_source(Preprocessor *pp)
{
    SourceStack *stack = pp->sources;
    if (stack->size == 0)
        return;
    SourceFrame *frame = &stack->frames[stack->size - 1];
    free_source_frame(frame);
    stack->size--;
    sync_location(pp);
}

static SourceFrame *current_frame(SourceStack *stack)
{
    if (stack->size == 0)
        return NULL;
    return &stack->frames[stack->size - 1];
}

static int source_getc(Preprocessor *pp)
{
    SourceStack *stack = pp->sources;
    while (stack->size > 0)
    {
        SourceFrame *frame = current_frame(stack);
        int ch = buffer_getc(frame->buffer);
        if (ch == EOF)
        {
            pop_source(pp);
            continue;
        }
        return ch;
    }
    return EOF;
}

static void source_ungetc(Preprocessor *pp, int ch)
{
    (void)ch; /* ByteBuffer版ではchは不要 */
    SourceStack *stack = pp->sources;
    SourceFrame *frame = current_frame(stack);
    if (!frame)
        return;
    buffer_ungetc(frame->buffer);
}

static void ensure_macro_stack_capacity(MacroExpansionStack *stack)
{
    if (stack->size == stack->capacity)
    {
        int new_cap = stack->capacity == 0 ? 4 : stack->capacity * 2;
        MacroExpansion *new_data =
            (MacroExpansion *)calloc(new_cap, sizeof(MacroExpansion));
        for (int i = 0; i < stack->size; i++)
            new_data[i] = stack->data[i];
        stack->data = new_data;
        stack->capacity = new_cap;
    }
}

static void push_expansion(MacroExpansionStack *stack, MacroExpansion exp)
{
    ensure_macro_stack_capacity(stack);
    stack->data[stack->size++] = exp;
}

static MacroExpansion *top_expansion(MacroExpansionStack *stack)
{
    if (stack->size == 0)
        return NULL;
    return &stack->data[stack->size - 1];
}

static void pop_expansion(MacroExpansionStack *stack)
{
    if (stack->size == 0)
        return;
    MacroExpansion *exp = &stack->data[--stack->size];
    free((void *)exp->text);
}

static Macro *find_macro(Macro *macros, const char *name)
{
    for (Macro *m = macros; m; m = m->next)
    {
        if (strcmp(m->name, name) == 0)
        {
            return m;
        }
    }
    return NULL;
}

static void free_macro(Macro *macro)
{
    if (!macro)
        return;
    free(macro->name);
    for (int i = 0; i < macro->param_count; ++i)
    {
        free(macro->params[i]);
    }
    free(macro->params);
    free(macro->body);
    free(macro);
}

static void add_macro(Preprocessor *pp, Macro *macro)
{
    Macro *existing = find_macro(pp->macros, macro->name);
    if (existing)
    {
        macro->next = existing->next;
        existing->next = NULL;
        free_macro(existing);
        if (pp->macros == existing)
        {
            pp->macros = macro;
        }
        else
        {
            Macro *iter = pp->macros;
            while (iter && iter->next != existing)
                iter = iter->next;
            if (iter)
                iter->next = macro;
            else
                macro->next = pp->macros;
        }
    }
    else
    {
        macro->next = pp->macros;
        pp->macros = macro;
    }
}

static void remove_macro(Preprocessor *pp, const char *name)
{
    Macro *prev = NULL;
    Macro *cur = pp->macros;
    while (cur)
    {
        if (strcmp(cur->name, name) == 0)
        {
            if (prev)
                prev->next = cur->next;
            else
                pp->macros = cur->next;
            free_macro(cur);
            return;
        }
        prev = cur;
        cur = cur->next;
    }
}

static void free_macros(Macro *macro)
{
    while (macro)
    {
        Macro *next = macro->next;
        free_macro(macro);
        macro = next;
    }
}

static void push_conditional(Preprocessor *pp, bool active, bool taken)
{
    ConditionalFrame *frame = (ConditionalFrame *)calloc(1, sizeof(ConditionalFrame));
    frame->active = active;
    frame->seen_true_branch = taken;
    frame->in_else = false;
    frame->next = pp->conditionals;
    pp->conditionals = frame;
}

static void pop_conditional(Preprocessor *pp)
{
    if (!pp->conditionals)
        return;
    ConditionalFrame *frame = pp->conditionals;
    pp->conditionals = frame->next;
    free(frame);
}

static ConditionalFrame *current_conditional(Preprocessor *pp)
{
    return pp->conditionals;
}

static void sync_location(Preprocessor *pp)
{
    if (!pp)
        return;
    SourceFrame *frame = current_frame(pp->sources);
    int line = frame ? frame->logical_line : (pp->scanner ? pp->scanner->creator->line_number : 1);
    const char *path = NULL;
    if (frame)
    {
        path = frame->logical_path;
    }
    else if (pp->scanner)
    {
        path = pp->scanner->initial_source_path;
    }
    if (frame == NULL && line <= 0)
        line = 1;
    if (pp->scanner)
    {
        pp->scanner->creator->line_number = line;
    }
}

static void advance_logical_line(Preprocessor *pp)
{
    if (!pp)
        return;
    SourceFrame *frame = current_frame(pp->sources);
    if (frame)
    {
        frame->logical_line++;
    }
    sync_location(pp);
}

static void mark_token_start(Preprocessor *pp)
{
    if (!pp)
        return;
    SourceFrame *frame = current_frame(pp->sources);
    if (frame)
    {
        pp->token_line = frame->logical_line;
        pp->token_path = frame->logical_path;
    }
    else
    {
        pp->token_line = pp->scanner ? pp->scanner->creator->line_number : 0;
        pp->token_path = NULL;
    }
}

static void ensure_yytext_capacity(Scanner *scanner)
{
    if (!scanner)
        return;
    if (scanner->yytext == NULL)
    {
        scanner->yt_max = BUFFER_GROW;
        scanner->ytp = 0;
        scanner->yytext = (char *)calloc(scanner->yt_max, sizeof(char));
    }
    else if (scanner->ytp >= scanner->yt_max - 1)
    {
        int old_max = scanner->yt_max;
        scanner->yt_max += BUFFER_GROW;
        char *new_buf = (char *)calloc(scanner->yt_max, sizeof(char));
        memcpy(new_buf, scanner->yytext, old_max);
        scanner->yytext = new_buf;
    }
}

static void addText(Scanner *scanner, char c)
{
    ensure_yytext_capacity(scanner);
    scanner->yytext[scanner->ytp++] = c;
    scanner->yytext[scanner->ytp] = '\0';
}

static void resetText(Scanner *scanner)
{
    if (!scanner)
        return;
    scanner->ytp = 0;
    if (scanner->yytext)
        scanner->yytext[0] = '\0';
}

static void push_expansion_text(Preprocessor *pp, const char *text, Macro *macro)
{
    if (!text)
        return;
    int len = strlen(text);
    bool ends_with_space = len > 0 && ascii_is_space(text[len - 1]);
    char last_nonspace = '\0';
    for (int i = len; i > 0; --i)
    {
        if (!ascii_is_space(text[i - 1]))
        {
            last_nonspace = text[i - 1];
            break;
        }
    }

    char *buffer = NULL;
    if (len > 0 && !ends_with_space && ascii_is_identchar(last_nonspace))
    {
        int next = get_raw_char(pp);
        if (next != EOF)
            unget_raw_char(pp, next);
        if (next != EOF && ascii_is_identchar((unsigned char)next))
        {
            buffer = (char *)calloc(len + 2, sizeof(char));
            strncpy(buffer, text, len);
            buffer[len] = ' ';
            buffer[len + 1] = '\0';
        }
    }
    MacroExpansion exp = {buffer ? buffer : dup_string(text), 0, macro};
    push_expansion(pp->expansions, exp);
    if (macro)
        macro->expanding = true;
}

static int get_raw_char_no_continuation(Preprocessor *pp)
{
    MacroExpansion *exp = top_expansion(pp->expansions);
    if (exp)
    {
        char ch = exp->text[exp->position];
        if (ch == '\0')
        {
            if (exp->macro)
                exp->macro->expanding = false;
            pop_expansion(pp->expansions);
            return get_raw_char(pp);
        }
        exp->position++;
        return ch;
    }

    int ch = source_getc(pp);
    if (ch == EOF)
        return EOF;
    return ch;
}

static void unget_raw_char(Preprocessor *pp, int ch)
{
    if (ch == '\n')
    {
        SourceFrame *frame = current_frame(pp->sources);
        if (frame && frame->logical_line > 1)
        {
            frame->logical_line--;
            sync_location(pp);
        }
    }
    MacroExpansion *exp = top_expansion(pp->expansions);
    if (exp)
    {
        if (exp->position > 0)
            exp->position--;
        return;
    }
    source_ungetc(pp, ch);
}

static int get_raw_char(Preprocessor *pp)
{
    while (1)
    {
        int ch = get_raw_char_no_continuation(pp);
        if (ch == '\\')
        {
            int next = get_raw_char_no_continuation(pp);
            if (next == '\n')
            {
                advance_logical_line(pp);
                continue;
            }
            if (next != EOF)
                unget_raw_char(pp, next);
        }
        return ch;
    }
}

static void skip_line_rest(Preprocessor *pp)
{
    int ch;
    while ((ch = get_raw_char(pp)) != EOF)
    {
        if (ch == '\n')
        {
            advance_logical_line(pp);
            pp->at_line_start = true;
            break;
        }
    }
}

static void append_char(char **buf, int *len, int *cap, char ch)
{
    if (*len + 1 >= *cap)
    {
        int old_cap = *cap;
        int new_cap = old_cap == 0 ? 16 : old_cap * 2;
        char *new_buf = (char *)calloc(new_cap, sizeof(char));
        if (!new_buf)
            return;
        if (*buf && old_cap > 0)
            memcpy(new_buf, *buf, old_cap);
        *buf = new_buf;
        *cap = new_cap;
    }
    (*buf)[(*len)++] = ch;
    (*buf)[*len] = '\0';
}

static void append_string(char **buf, int *len, int *cap, const char *src)
{
    while (*src)
    {
        append_char(buf, len, cap, *src++);
    }
}

static int peek_char(Preprocessor *pp)
{
    int ch = get_raw_char(pp);
    if (ch != EOF)
        unget_raw_char(pp, ch);
    return ch;
}

static int next_nonspace_char(Preprocessor *pp)
{
    int ch;
    while ((ch = get_raw_char(pp)) != EOF)
    {
        if (!ascii_is_space((unsigned char)ch))
            return ch;
        if (ch == '\n')
            advance_logical_line(pp);
    }
    return EOF;
}

static char *read_line_raw(Preprocessor *pp)
{
    char *buf = NULL;
    int len = 0;
    int cap = 0;
    int ch;
    bool continue_line = true;
    while (continue_line && (ch = get_raw_char(pp)) != EOF)
    {
        if (ch == '\\')
        {
            int next = get_raw_char(pp);
            if (next == '\n')
            {
                advance_logical_line(pp);
                continue_line = true;
                continue;
            }
            else
            {
                unget_raw_char(pp, next);
            }
        }
        if (ch == '\n')
        {
            advance_logical_line(pp);
            append_char(&buf, &len, &cap, '\n');
            break;
        }
        append_char(&buf, &len, &cap, (char)ch);
    }
    if (!buf)
    {
        buf = dup_string("");
    }
    return buf;
}

static char *trim_leading(char *s)
{
    while (*s && ascii_is_space(*s))
        s++;
    return s;
}

static char *parse_identifier(char **cursor)
{
    char *start = *cursor;
    while (**cursor && (ascii_is_alnum(**cursor) || **cursor == '_'))
    {
        (*cursor)++;
    }
    int len = (int)(*cursor - start);
    if (len == 0)
    {
        return NULL;
    }
    char *id = (char *)calloc(len + 1, sizeof(char));
    strncpy(id, start, len);
    id[len] = '\0';
    return id;
}

static Macro *create_macro(const char *name)
{
    Macro *macro = (Macro *)calloc(1, sizeof(Macro));
    macro->name = dup_string(name);
    return macro;
}

static char *join_dir_and_path(const char *dir, const char *path)
{
    int dir_len = strlen(dir);
    int path_len = strlen(path);
    int total = dir_len + 1 + path_len + 1;
    char *full = (char *)calloc(total, sizeof(char));
    snprintf(full, total, "%s/%s", dir, path);
    return full;
}

static void skip_whitespace_internal(const char **cursor)
{
    while (**cursor && ascii_is_space(**cursor))
        (*cursor)++;
}

typedef enum
{
    PP_TOKEN_IDENTIFIER,
    PP_TOKEN_NUMBER,
    PP_TOKEN_STRING,
    PP_TOKEN_CHAR,
    PP_TOKEN_WHITESPACE,
    PP_TOKEN_OTHER,
} PreprocessorTokenType;

typedef struct
{
    PreprocessorToken *data;
    int size;
    int capacity;
} TokenArray;

static void free_tokens(PreprocessorToken *tokens, int count)
{
    if (!tokens)
        return;
    for (int i = 0; i < count; ++i)
    {
        free(tokens[i].text);
    }
    free(tokens);
}

static void free_token_array(TokenArray *arr)
{
    free_tokens(arr->data, arr->size);
    arr->data = NULL;
    arr->size = arr->capacity = 0;
}

static void ensure_token_capacity(TokenArray *arr)
{
    if (arr->size == arr->capacity)
    {
        int new_cap = arr->capacity == 0 ? 8 : arr->capacity * 2;
        PreprocessorToken *new_data = (PreprocessorToken *)calloc(new_cap, sizeof(PreprocessorToken));
        for (int i = 0; i < arr->size; i++)
            new_data[i] = arr->data[i];
        arr->data = new_data;
        arr->capacity = new_cap;
    }
}

static void push_token(TokenArray *arr, PreprocessorTokenType type, const char *start,
                       int len)
{
    ensure_token_capacity(arr);
    PreprocessorToken *tok = &arr->data[arr->size++];
    tok->type = type;
    tok->text = (char *)calloc(len + 1, sizeof(char));
    strncpy(tok->text, start, len);
    tok->text[len] = '\0';
    tok->int_value = 0;
}

static bool token_is_whitespace(const PreprocessorToken *tok)
{
    return tok->type == PP_TOKEN_WHITESPACE;
}

static bool token_is_ident_like(const PreprocessorToken *tok)
{
    return tok->type == PP_TOKEN_IDENTIFIER || tok->type == PP_TOKEN_NUMBER;
}

static bool starts_with_ident_char(const PreprocessorToken *tok)
{
    if (!tok->text || tok->text[0] == '\0')
        return false;
    return ascii_is_identchar(tok->text[0]);
}

static bool ends_with_ident_char(const PreprocessorToken *tok)
{
    if (!tok->text)
        return false;
    int len = strlen(tok->text);
    if (len == 0)
        return false;
    char ch = tok->text[len - 1];
    return ascii_is_identchar(ch);
}

static TokenArray tokenize_text(const char *text)
{
    TokenArray arr = {};
    const char *p = text ? text : "";
    while (*p)
    {
        if (ascii_is_space(*p))
        {
            const char *start = p;
            while (*p && ascii_is_space(*p))
                p++;
            push_token(&arr, PP_TOKEN_WHITESPACE, start, (int)(p - start));
            continue;
        }
        if (*p == '\"')
        {
            const char *start = p++;
            while (*p)
            {
                if (*p == '\\' && p[1])
                {
                    p += 2;
                    continue;
                }
                if (*p == '\"')
                {
                    p++;
                    break;
                }
                p++;
            }
            push_token(&arr, PP_TOKEN_STRING, start, (int)(p - start));
            continue;
        }
        if (*p == '\'')
        {
            const char *start = p++;
            while (*p)
            {
                if (*p == '\\' && p[1])
                {
                    p += 2;
                    continue;
                }
                if (*p == '\'')
                {
                    p++;
                    break;
                }
                p++;
            }
            push_token(&arr, PP_TOKEN_CHAR, start, (int)(p - start));
            continue;
        }
        if (ascii_is_digit(*p))
        {
            const char *start = p;
            while (*p && (ascii_is_alnum(*p) || *p == '.'))
                p++;
            push_token(&arr, PP_TOKEN_NUMBER, start, (int)(p - start));
            continue;
        }
        if (ascii_is_identchar(*p))
        {
            const char *start = p;
            while (*p && ascii_is_identchar(*p))
                p++;
            push_token(&arr, PP_TOKEN_IDENTIFIER, start, (int)(p - start));
            continue;
        }
        push_token(&arr, PP_TOKEN_OTHER, p, 1);
        p++;
    }
    return arr;
}

static void append_tokens(TokenArray *dest, const PreprocessorToken *src, int count)
{
    for (int i = 0; i < count; ++i)
    {
        push_token(dest, (PreprocessorTokenType)src[i].type, src[i].text,
                   strlen(src[i].text));
    }
}

static char *tokens_to_text(const TokenArray *tokens)
{
    char *result = NULL;
    int len = 0;
    int cap = 0;
    for (int i = 0; i < tokens->size; ++i)
    {
        const PreprocessorToken *tok = &tokens->data[i];
        append_string(&result, &len, &cap, tok->text);
        if (i + 1 < tokens->size)
        {
            const PreprocessorToken *next = &tokens->data[i + 1];
            if (!token_is_whitespace(tok) && !token_is_whitespace(next) &&
                ends_with_ident_char(tok) && starts_with_ident_char(next))
            {
                append_char(&result, &len, &cap, ' ');
            }
        }
    }
    if (!result)
        return dup_string("");
    return result;
}

static MacroArgument *parse_macro_arguments(Preprocessor *pp)
{
    MacroArgument *head = NULL;
    MacroArgument *last = NULL;
    int depth = 0;
    int len = 0;
    int cap = 0;
    char *buf = NULL;
    bool started = false;
    while (1)
    {
        int ch = get_raw_char(pp);
        if (ch == EOF)
            break;
        if (!started && ascii_is_space((unsigned char)ch))
            continue;
        started = true;
        if (ch == '(')
        {
            depth++;
            append_char(&buf, &len, &cap, (char)ch);
            continue;
        }
        if (ch == ')')
        {
            if (depth == 0)
            {
                MacroArgument *arg = (MacroArgument *)calloc(1, sizeof(MacroArgument));
                arg->text = buf ? buf : dup_string("");
                TokenArray tokens = tokenize_text(arg->text);
                arg->tokens = tokens.data;
                arg->token_count = tokens.size;
                arg->next = NULL;
                if (head == NULL)
                {
                    head = arg;
                }
                else
                {
                    last->next = arg;
                }
                last = arg;
                return head;
            }
            depth--;
        }
        if (ch == ',' && depth == 0)
        {
            MacroArgument *arg = (MacroArgument *)calloc(1, sizeof(MacroArgument));
            arg->text = buf ? buf : dup_string("");
            TokenArray tokens = tokenize_text(arg->text);
            arg->tokens = tokens.data;
            arg->token_count = tokens.size;
            arg->next = NULL;
            if (head == NULL)
            {
                head = arg;
            }
            else
            {
                last->next = arg;
            }
            last = arg;
            buf = NULL;
            len = cap = 0;
            continue;
        }
        append_char(&buf, &len, &cap, (char)ch);
    }
    return head;
}

static void free_macro_arguments(MacroArgument *arg)
{
    while (arg)
    {
        MacroArgument *next = arg->next;
        free(arg->text);
        free_tokens(arg->tokens, arg->token_count);
        free(arg);
        arg = next;
    }
}

static char *substitute_macro_body(Macro *macro, MacroArgument *args, SourceFrame *frame)
{
    if (macro->builtin_file)
    {
        const char *logical_path = frame->logical_path ? frame->logical_path : frame->path;
        TokenArray tokens = {};
        int len = strlen(logical_path) + 3;
        char *buf = (char *)calloc(len, sizeof(char));
        snprintf(buf, len, "\"%s\"", logical_path);
        push_token(&tokens, PP_TOKEN_STRING, buf, strlen(buf));
        char *text = tokens_to_text(&tokens);
        free_token_array(&tokens);
        return text;
    }
    if (macro->builtin_line)
    {
        char tmp[32];
        snprintf(tmp, sizeof tmp, "%d", frame->logical_line);
        TokenArray tokens = {};
        push_token(&tokens, PP_TOKEN_NUMBER, tmp, strlen(tmp));
        char *text = tokens_to_text(&tokens);
        free_token_array(&tokens);
        return text;
    }

    TokenArray body_tokens = tokenize_text(macro->body);
    TokenArray result_tokens = {};
    for (int i = 0; i < body_tokens.size; ++i)
    {
        PreprocessorToken *tok = &body_tokens.data[i];
        bool replaced = false;
        if (macro->is_function && tok->type == PP_TOKEN_IDENTIFIER)
        {
            /* Check for __VA_ARGS__ in variadic macro */
            if (macro->is_variadic && strcmp(tok->text, "__VA_ARGS__") == 0)
            {
                /* Skip to variadic arguments (after fixed params) */
                MacroArgument *arg = args;
                for (int idx = 0; idx < macro->param_count && arg; ++idx)
                    arg = arg->next;
                /* Append all variadic arguments separated by commas */
                bool first = true;
                while (arg)
                {
                    if (!first)
                    {
                        push_token(&result_tokens, PP_TOKEN_OTHER, ",", 1);
                        push_token(&result_tokens, PP_TOKEN_WHITESPACE, " ", 1);
                    }
                    if (arg->tokens)
                    {
                        append_tokens(&result_tokens, arg->tokens, arg->token_count);
                    }
                    first = false;
                    arg = arg->next;
                }
                replaced = true;
            }
            else
            {
                for (int p = 0; p < macro->param_count; ++p)
                {
                    if (strcmp(macro->params[p], tok->text) == 0)
                    {
                        MacroArgument *arg = args;
                        for (int idx = 0; idx < p && arg; ++idx)
                            arg = arg->next;
                        if (arg && arg->tokens)
                        {
                            append_tokens(&result_tokens, arg->tokens, arg->token_count);
                        }
                        replaced = true;
                        break;
                    }
                }
            }
        }
        if (!replaced)
        {
            push_token(&result_tokens, (PreprocessorTokenType)tok->type, tok->text,
                       strlen(tok->text));
        }
    }

    char *text = tokens_to_text(&result_tokens);
    free_token_array(&body_tokens);
    free_token_array(&result_tokens);
    return text;
}

static void handle_include(Preprocessor *pp, char *arg_line)
{
    char *cursor = trim_leading(arg_line);
    char terminator;
    if (*cursor == '\"')
        terminator = '\"';
    else if (*cursor == '<')
        terminator = '>';
    else
    {
        skip_line_rest(pp);
        return;
    }
    cursor++;
    char *start = cursor;
    while (*cursor && *cursor != terminator)
        cursor++;
    *cursor = '\0';

    char *header_path = start;

    /* Check if it's an embedded header */
    const char *name = strrchr(header_path, '/');
    name = name ? name + 1 : header_path;
    bool is_embedded = (load_embedded(name) != NULL);

    char *resolved = NULL;

    /* Resolve path for non-embedded headers */
    if (!is_embedded)
    {
        SourceFrame *frame = current_frame(pp->sources);
        /* 現在のファイルのディレクトリからのパスを構築 */
        if (frame)
        {
            resolved = join_dir_and_path(frame->dir, header_path);
        }
        else
        {
            resolved = dup_string(header_path);
        }
    }

    /* Record dependency instead of expanding */
    const char *dep_path = is_embedded ? header_path : resolved;
    pp_add_dependency(pp, dep_path, is_embedded);

    if (resolved)
        free(resolved);
}

static void handle_define(Preprocessor *pp, char *line)
{
    char *cursor = trim_leading(line);
    if (!*cursor)
        return;
    char *name = parse_identifier(&cursor);
    if (!name)
        return;
    Macro *macro = create_macro(name);
    free(name);
    if (*cursor == '(')
    {
        macro->is_function = true;
        cursor++;
        skip_whitespace_internal((const char **)(&cursor));
        while (*cursor && *cursor != ')')
        {
            /* Skip variadic "..." parameter */
            if (cursor[0] == '.' && cursor[1] == '.' && cursor[2] == '.')
            {
                macro->is_variadic = true;
                cursor += 3;
                skip_whitespace_internal((const char **)(&cursor));
                if (*cursor == ',')
                {
                    cursor++;
                    skip_whitespace_internal((const char **)(&cursor));
                }
                continue;
            }
            char *param = parse_identifier(&cursor);
            if (param)
            {
                int new_count = macro->param_count + 1;
                char **new_params = (char **)calloc(new_count, sizeof(char *));
                for (int i = 0; i < macro->param_count; i++)
                    new_params[i] = macro->params[i];
                macro->params = new_params;
                macro->params[macro->param_count++] = param;
            }
            else
            {
                /* Unknown character in parameter list, skip to avoid infinite loop */
                break;
            }
            skip_whitespace_internal((const char **)(&cursor));
            if (*cursor == ',')
            {
                cursor++;
                skip_whitespace_internal((const char **)(&cursor));
            }
        }
        if (*cursor == ')')
            cursor++;
    }
    else
    {
        cursor = trim_leading(cursor);
    }
    cursor = trim_leading(cursor);
    if (*cursor)
    {
        macro->body = dup_string(cursor);
        /* Remove trailing newline from macro body */
        int body_len = strlen(macro->body);
        while (body_len > 0 && (macro->body[body_len - 1] == '\n' ||
                                macro->body[body_len - 1] == '\r'))
        {
            macro->body[--body_len] = '\0';
        }
    }
    add_macro(pp, macro);
}

static void handle_undef(Preprocessor *pp, char *line)
{
    char *cursor = trim_leading(line);
    char *name = parse_identifier(&cursor);
    if (name)
    {
        remove_macro(pp, name);
        free(name);
    }
}

static void handle_line_directive(Preprocessor *pp, char *line)
{
    if (!pp)
        return;
    SourceFrame *frame = current_frame(pp->sources);
    if (!frame)
        return;

    char *cursor = trim_leading(line);
    if (!*cursor)
        return;

    char *endptr = cursor;
    long value = strtol(cursor, &endptr, 10);
    if (endptr == cursor)
        return;

    int new_line = (int)value;
    if (new_line < 1)
        new_line = 1;
    frame->logical_line = new_line;
    sync_location(pp);

    cursor = trim_leading(endptr);
    if (*cursor == '"')
    {
        cursor++;
        char *filename = NULL;
        int len = 0;
        int cap = 0;
        while (*cursor && *cursor != '"')
        {
            char ch = *cursor++;
            if (ch == '\\' && *cursor)
            {
                ch = *cursor++;
            }
            append_char(&filename, &len, &cap, ch);
        }
        if (*cursor == '"')
        {
            cursor++;
        }
        if (!filename)
        {
            filename = dup_string("");
        }
        char *retained = retain_string(pp, filename);
        free(filename);
        if (retained)
        {
            frame->logical_path = retained;
        }
    }
}

static bool macro_defined(Preprocessor *pp, const char *name)
{
    return find_macro(pp->macros, name) != NULL;
}

static void handle_ifdef(Preprocessor *pp, char *line, bool negate)
{
    char *cursor = trim_leading(line);
    char *name = parse_identifier(&cursor);
    bool defined = name && macro_defined(pp, name);
    free(name);
    ConditionalFrame *parent = current_conditional(pp);
    bool active = (!parent || parent->active) && (negate ? !defined : defined);
    push_conditional(pp, active, active);
}

/* #if expression parser - recursive descent */
static int eval_or_expr(Preprocessor *pp, const char **cursor);

static void skip_ws(const char **cursor)
{
    while (**cursor && ascii_is_space(**cursor))
        (*cursor)++;
}

static int eval_primary(Preprocessor *pp, const char **cursor)
{
    skip_ws(cursor);

    /* defined(X) or defined X */
    if (strncmp(*cursor, "defined", 7) == 0 &&
        !ascii_is_identchar((*cursor)[7]))
    {
        *cursor += 7;
        skip_ws(cursor);
        bool has_paren = false;
        if (**cursor == '(')
        {
            has_paren = true;
            (*cursor)++;
            skip_ws(cursor);
        }
        char *name = parse_identifier((char **)cursor);
        int result = name && macro_defined(pp, name) ? 1 : 0;
        free(name);
        if (has_paren)
        {
            skip_ws(cursor);
            if (**cursor == ')')
                (*cursor)++;
        }
        return result;
    }

    /* Parenthesized expression */
    if (**cursor == '(')
    {
        (*cursor)++;
        int result = eval_or_expr(pp, cursor);
        skip_ws(cursor);
        if (**cursor == ')')
            (*cursor)++;
        return result;
    }

    /* Number literal */
    if (ascii_is_digit(**cursor))
    {
        int val = 0;
        while (ascii_is_digit(**cursor))
        {
            val = val * 10 + (**cursor - '0');
            (*cursor)++;
        }
        /* Skip suffix like L, U, etc. */
        while (**cursor == 'L' || **cursor == 'l' ||
               **cursor == 'U' || **cursor == 'u')
            (*cursor)++;
        return val;
    }

    /* Identifier - undefined macro evaluates to 0 */
    if (ascii_is_identchar(**cursor))
    {
        char *name = parse_identifier((char **)cursor);
        /* In #if, undefined identifiers are 0 */
        free(name);
        return 0;
    }

    return 0;
}

static int eval_unary(Preprocessor *pp, const char **cursor)
{
    skip_ws(cursor);
    if (**cursor == '!')
    {
        (*cursor)++;
        return !eval_unary(pp, cursor);
    }
    return eval_primary(pp, cursor);
}

static int eval_and_expr(Preprocessor *pp, const char **cursor)
{
    int left = eval_unary(pp, cursor);
    while (1)
    {
        skip_ws(cursor);
        if ((*cursor)[0] == '&' && (*cursor)[1] == '&')
        {
            *cursor += 2;
            int right = eval_unary(pp, cursor);
            left = (left && right) ? 1 : 0;
        }
        else
        {
            break;
        }
    }
    return left;
}

static int eval_or_expr(Preprocessor *pp, const char **cursor)
{
    int left = eval_and_expr(pp, cursor);
    while (1)
    {
        skip_ws(cursor);
        if ((*cursor)[0] == '|' && (*cursor)[1] == '|')
        {
            *cursor += 2;
            int right = eval_and_expr(pp, cursor);
            left = (left || right) ? 1 : 0;
        }
        else
        {
            break;
        }
    }
    return left;
}

static int eval_if_expr(Preprocessor *pp, const char *expr)
{
    const char *cursor = expr;
    return eval_or_expr(pp, &cursor);
}

static void handle_if(Preprocessor *pp, char *line)
{
    char *cursor = trim_leading(line);
    int result = eval_if_expr(pp, cursor);
    ConditionalFrame *parent = current_conditional(pp);
    bool active = (!parent || parent->active) && (result != 0);
    push_conditional(pp, active, active);
}

static void handle_elif(Preprocessor *pp, char *line)
{
    ConditionalFrame *frame = current_conditional(pp);
    if (!frame)
        return;
    if (frame->in_else)
        return;

    bool parent_active = !frame->next || frame->next->active;
    if (frame->seen_true_branch || !parent_active)
    {
        frame->active = false;
    }
    else
    {
        char *cursor = trim_leading(line);
        int result = eval_if_expr(pp, cursor);
        frame->active = (result != 0);
        if (frame->active)
            frame->seen_true_branch = true;
    }
}

static void handle_else(Preprocessor *pp)
{
    ConditionalFrame *frame = current_conditional(pp);
    if (!frame)
        return;
    if (frame->in_else)
        return;
    frame->in_else = true;
    bool parent_active = !frame->next || frame->next->active;
    frame->active = parent_active && !frame->seen_true_branch;
    frame->seen_true_branch = frame->seen_true_branch || frame->active;
}

static bool current_block_active(Preprocessor *pp)
{
    ConditionalFrame *frame = current_conditional(pp);
    if (!frame)
        return true;
    return frame->active;
}

static void process_directive(Preprocessor *pp)
{
    char *line = read_line_raw(pp);
    char *cursor = trim_leading(line);
    if (strncmp(cursor, "include", 7) == 0)
    {
        cursor += 7;
        if (current_block_active(pp))
            handle_include(pp, cursor);
    }
    else if (strncmp(cursor, "define", 6) == 0)
    {
        cursor += 6;
        if (current_block_active(pp))
            handle_define(pp, cursor);
    }
    else if (strncmp(cursor, "undef", 5) == 0)
    {
        cursor += 5;
        if (current_block_active(pp))
            handle_undef(pp, cursor);
    }
    else if (strncmp(cursor, "line", 4) == 0 &&
             (cursor[4] == '\0' || ascii_is_space((unsigned char)cursor[4])))
    {
        cursor += 4;
        if (current_block_active(pp))
            handle_line_directive(pp, cursor);
    }
    else if (strncmp(cursor, "ifdef", 5) == 0)
    {
        cursor += 5;
        handle_ifdef(pp, cursor, false);
    }
    else if (strncmp(cursor, "ifndef", 6) == 0)
    {
        cursor += 6;
        handle_ifdef(pp, cursor, true);
    }
    else if (strncmp(cursor, "if", 2) == 0 &&
             !ascii_is_identchar(cursor[2]))
    {
        cursor += 2;
        handle_if(pp, cursor);
    }
    else if (strncmp(cursor, "elif", 4) == 0 &&
             !ascii_is_identchar(cursor[4]))
    {
        cursor += 4;
        handle_elif(pp, cursor);
    }
    else if (strncmp(cursor, "else", 4) == 0 &&
             !ascii_is_identchar(cursor[4]))
    {
        handle_else(pp);
    }
    else if (strncmp(cursor, "endif", 5) == 0 &&
             !ascii_is_identchar(cursor[5]))
    {
        pop_conditional(pp);
    }
    else if (strncmp(cursor, "pragma", 6) == 0)
    {
        /* Ignore #pragma directives */
    }
    free(line);
}

static int preprocess_next_char(Preprocessor *pp)
{
    while (1)
    {
        int ch = get_raw_char(pp);
        if (ch == EOF)
            return EOF;

        if (!current_block_active(pp) && ch != '\n' && !(pp->at_line_start && ch == '#'))
        {
            continue;
        }

        if (ch == '\n')
        {
            pp->at_line_start = true;
            advance_logical_line(pp);
            return ch;
        }

        if (pp->at_line_start && ch == '#')
        {
            process_directive(pp);
            pp->at_line_start = true;
            continue;
        }

        if (ch == '/')
        {
            int next = get_raw_char(pp);
            if (next == '/')
            {
                while ((ch = get_raw_char(pp)) != EOF && ch != '\n')
                    ;
                if (ch == '\n')
                {
                    pp->at_line_start = true;
                    advance_logical_line(pp);
                    return '\n';
                }
                return EOF;
            }
            else if (next == '*')
            {
                int prev = 0;
                while ((ch = get_raw_char(pp)) != EOF)
                {
                    if (prev == '*' && ch == '/')
                        break;
                    if (ch == '\n')
                    {
                        advance_logical_line(pp);
                        pp->at_line_start = true;
                    }
                    prev = ch;
                }
                continue;
            }
            else
            {
                unget_raw_char(pp, next);
            }
        }

        /* Only clear at_line_start for non-whitespace characters */
        if (!ascii_is_space((unsigned char)ch))
        {
            pp->at_line_start = false;
        }
        return ch;
    }
}

static int peek_nonspace(Preprocessor *pp, char *buffer, int *len)
{
    int ch;
    *len = 0;
    while ((ch = preprocess_next_char(pp)) != EOF)
    {
        if (!ascii_is_space((unsigned char)ch))
        {
            break;
        }
        buffer[(*len)++] = (char)ch;
    }
    return ch;
}

static bool try_expand_macro(Preprocessor *pp, const char *ident)
{
    Macro *macro = find_macro(pp->macros, ident);
    if (!macro || macro->expanding)
        return false;
    SourceFrame *frame = current_frame(pp->sources);
    if (!frame)
        return false;
    if (macro->is_function)
    {
        char consumed_ws[16];
        int ws_len = 0;
        int ch = peek_nonspace(pp, consumed_ws, &ws_len);
        if (ch != '(')
        {
            for (int i = 0; i < ws_len; ++i)
            {
                unget_raw_char(pp, consumed_ws[ws_len - 1 - i]);
            }
            if (ch != EOF)
                unget_raw_char(pp, ch);
            return false;
        }
        MacroArgument *args = parse_macro_arguments(pp);
        char *expanded = substitute_macro_body(macro, args, frame);
        free_macro_arguments(args);
        if (expanded)
        {
            push_expansion_text(pp, expanded, macro);
        }
        free(expanded);
    }
    else
    {
        char *expanded = substitute_macro_body(macro, NULL, frame);
        push_expansion_text(pp, expanded, macro);
        free(expanded);
    }
    return true;
}

static void ensure_initial_source(Preprocessor *pp)
{
    if (pp->sources->size == 0)
    {
        const char *path = pp->initial_source_path ? pp->initial_source_path : "";
        ByteBuffer *buffer = pp->initial_buffer;
        if (!buffer)
        {
            buffer = create_buffer(NULL, 0, false);
        }
        push_source(pp, path, buffer);
        pp->initial_buffer = NULL; /* 所有権をsourcesスタックに移譲 */
    }
}

static int read_char(Preprocessor *pp)
{
    ensure_initial_source(pp);
    return preprocess_next_char(pp);
}

static void ensure_capacity(char **buffer, int *capacity, int required)
{
    if (*capacity >= required)
    {
        return;
    }

    int old_capacity = *capacity;
    int new_capacity = old_capacity ? old_capacity : 64;
    while (new_capacity < required)
    {
        new_capacity *= 2;
    }

    char *new_buffer = (char *)calloc(new_capacity, sizeof(char));
    if (!new_buffer)
    {
        *buffer = NULL;
        *capacity = 0;
        return;
    }
    if (*buffer && old_capacity > 0)
        memcpy(new_buffer, *buffer, old_capacity);

    *buffer = new_buffer;
    *capacity = new_capacity;
}

static char *read_balanced_attribute(Preprocessor *pp)
{
    int paren_depth = 0;
    int brace_depth = 0;
    int bracket_depth = 0;
    bool in_string = false;
    bool in_char = false;
    bool escape = false;

    int capacity = 0;
    int length = 0;
    char *buffer = NULL;

    while (1)
    {
        int c = read_char(pp);
        if (c == EOF || c == -1)
        {
            free(buffer);
            return NULL;
        }

        if (in_string)
        {
            if (escape)
            {
                escape = false;
                goto append_char;
            }
            if (c == '\\')
            {
                escape = true;
                goto append_char;
            }
            if (c == '\"')
            {
                in_string = false;
            }
            goto append_char;
        }
        if (in_char)
        {
            if (escape)
            {
                escape = false;
                goto append_char;
            }
            if (c == '\\')
            {
                escape = true;
                goto append_char;
            }
            if (c == '\'')
            {
                in_char = false;
            }
            goto append_char;
        }

        switch (c)
        {
        case '\"':
            in_string = true;
            break;
        case '\'':
            in_char = true;
            break;
        case '(':
            ++paren_depth;
            break;
        case ')':
            if (paren_depth > 0)
                --paren_depth;
            break;
        case '{':
            ++brace_depth;
            break;
        case '}':
            if (brace_depth > 0)
                --brace_depth;
            break;
        case '[':
            ++bracket_depth;
            break;
        case ']':
        {
            if (bracket_depth > 0)
            {
                --bracket_depth;
                break;
            }
            if (paren_depth == 0 && brace_depth == 0 && bracket_depth == 0)
            {
                int next = read_char(pp);
                if (next == ']')
                {
                    if (buffer)
                    {
                        ensure_capacity(&buffer, &capacity, length + 1);
                        if (!buffer)
                        {
                            return NULL;
                        }
                        buffer[length] = '\0';
                    }
                    return buffer;
                }
                pushback_char(pp, next);
            }
            break;
        }
        default:
            break;
        }

    append_char:
        ensure_capacity(&buffer, &capacity, length + 2);
        if (!buffer)
        {
            return NULL;
        }
        buffer[length++] = (char)c;
    }
}

static void pushback_char(Preprocessor *pp, int ch)
{
    unget_raw_char(pp, ch);
}

static void error(Scanner *scanner)
{
    fprintf(stderr, "cannot understand character: %s\n", scanner->yytext);
    exit(1);
}

Preprocessor *pp_create(Scanner *scanner)
{
    Preprocessor *pp = (Preprocessor *)calloc(1, sizeof(Preprocessor));
    pp->scanner = scanner;
    pp->sources = (SourceStack *)calloc(1, sizeof(SourceStack));
    init_source_stack(pp->sources);
    pp->expansions = (MacroExpansionStack *)calloc(1, sizeof(MacroExpansionStack));
    pp->at_line_start = true;
    pp->initial_source_path = dup_string("stdin");
    pp->token_line = 1;
    pp->token_path = pp->initial_source_path;

    Macro *file_macro = (Macro *)calloc(1, sizeof(Macro));
    file_macro->name = dup_string("__FILE__");
    file_macro->builtin_file = true;
    add_macro(pp, file_macro);

    Macro *line_macro = (Macro *)calloc(1, sizeof(Macro));
    line_macro->name = dup_string("__LINE__");
    line_macro->builtin_line = true;
    add_macro(pp, line_macro);

    /* va_arg(ap, type) → __builtin_va_arg(ap, sizeof(type)) */
    Macro *va_arg_macro = (Macro *)calloc(1, sizeof(Macro));
    va_arg_macro->name = dup_string("va_arg");
    va_arg_macro->is_function = true;
    va_arg_macro->param_count = 2;
    va_arg_macro->params = (char **)calloc(2, sizeof(char *));
    va_arg_macro->params[0] = dup_string("ap");
    va_arg_macro->params[1] = dup_string("type");
    va_arg_macro->body = dup_string("__builtin_va_arg(ap, sizeof(type))");
    add_macro(pp, va_arg_macro);

    return pp;
}

void pp_destroy(Preprocessor *pp)
{
    if (!pp)
        return;
    cleanup_source_stack(pp->sources);
    free_macros(pp->macros);
    for (int i = 0; i < pp->expansions->size; ++i)
    {
        free((void *)pp->expansions->data[i].text);
    }
    free(pp->expansions->data);
    for (int i = 0; i < pp->include_dir_count; ++i)
    {
        free(pp->include_dirs[i]);
    }
    free(pp->include_dirs);
    while (pp->conditionals)
        pop_conditional(pp);
    for (int i = 0; i < pp->retained_string_count; ++i)
    {
        free(pp->retained_strings[i]);
    }
    free(pp->retained_strings);
    /* Free dependencies */
    for (int i = 0; i < pp->dependency_count; ++i)
    {
        free(pp->dependencies[i].path);
    }
    free(pp->dependencies);
    free(pp->initial_source_path);
    if (pp->initial_buffer)
        free_buffer(pp->initial_buffer);
    free(pp);
}

void pp_set_initial_source(Preprocessor *pp, const char *path, ByteBuffer *buffer)
{
    if (!pp)
        return;
    free(pp->initial_source_path);
    pp->initial_source_path = dup_string(path ? path : "stdin");
    if (pp->initial_buffer)
        free_buffer(pp->initial_buffer);
    pp->initial_buffer = buffer;
}

void pp_add_include_dir(Preprocessor *pp, const char *path)
{
    if (!pp || !path)
        return;
    if (pp->include_dir_count == pp->include_dir_capacity)
    {
        int new_cap = pp->include_dir_capacity == 0 ? 4 : pp->include_dir_capacity * 2;
        char **new_dirs = (char **)calloc(new_cap, sizeof(char *));
        for (int i = 0; i < pp->include_dir_count; i++)
            new_dirs[i] = pp->include_dirs[i];
        pp->include_dirs = new_dirs;
        pp->include_dir_capacity = new_cap;
    }
    pp->include_dirs[pp->include_dir_count++] = dup_string(path);
}

/* Check if dependency already exists */
static bool pp_has_dependency(Preprocessor *pp, const char *path)
{
    for (int i = 0; i < pp->dependency_count; ++i)
    {
        if (strcmp(pp->dependencies[i].path, path) == 0)
        {
            return true;
        }
    }
    return false;
}

void pp_add_dependency(Preprocessor *pp, const char *path, bool is_embedded)
{
    if (!pp || !path)
        return;
    /* Skip if already added */
    if (pp_has_dependency(pp, path))
        return;
    if (pp->dependency_count == pp->dependency_capacity)
    {
        int new_cap = pp->dependency_capacity == 0 ? 8 : pp->dependency_capacity * 2;
        PP_Dependency *new_deps = (PP_Dependency *)calloc(new_cap, sizeof(PP_Dependency));
        for (int i = 0; i < pp->dependency_count; i++)
            new_deps[i] = pp->dependencies[i];
        pp->dependencies = new_deps;
        pp->dependency_capacity = new_cap;
    }
    PP_Dependency *dep = &pp->dependencies[pp->dependency_count++];
    dep->path = dup_string(path);
    dep->is_embedded = is_embedded;
}

int pp_get_dependency_count(Preprocessor *pp)
{
    return pp ? pp->dependency_count : 0;
}

const PP_Dependency *pp_get_dependency(Preprocessor *pp, int index)
{
    if (!pp || index >= pp->dependency_count)
        return NULL;
    return &pp->dependencies[index];
}

static int scan_token(Preprocessor *pp, YYSTYPE *yylval)
{
    Scanner *scanner = pp->scanner;
    char c;
    resetText(scanner);

retry:
    c = read_char(pp);
    if (c == ' ' || c == '\t' || c == '\n')
    {
        goto retry;
    }
    mark_token_start(pp);
    switch (c)
    {
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    {
        addText(scanner, c);
        int is_hex = 0;
        if (c == '0')
        {
            c = read_char(pp);
            if (c == 'x' || c == 'X')
            {
                is_hex = 1;
                addText(scanner, c);
                while (1)
                {
                    c = read_char(pp);
                    if (ascii_is_digit(c) ||
                        (c >= 'a' && c <= 'f') ||
                        (c >= 'A' && c <= 'F'))
                    {
                        addText(scanner, c);
                    }
                    else
                    {
                        break;
                    }
                }
                /* Check for U/u and/or L/l suffix */
                int is_unsigned = 0;
                int is_long = 0;
                if (c == 'U' || c == 'u')
                {
                    is_unsigned = 1;
                    c = read_char(pp);
                }
                if (c == 'L' || c == 'l')
                {
                    is_long = 1;
                    c = read_char(pp);
                }
                /* Handle LU order as well (e.g., 0x1LU) */
                if (!is_unsigned && (c == 'U' || c == 'u'))
                {
                    is_unsigned = 1;
                    c = read_char(pp);
                }
                pushback_char(pp, c);

                /* Always parse as long to handle overflow */
                unsigned long l_value;
                sscanf(scanner->yytext, "%lx", &l_value);

                if (is_long)
                {
                    yylval->lv = (long)l_value;
                    return is_unsigned ? ULONG_LITERAL : LONG_LITERAL;
                }
                else
                {
                    yylval->iv = (int)l_value;
                    return is_unsigned ? UINT_LITERAL : INT_LITERAL;
                }
            }
            else
            {
                pushback_char(pp, c);
                c = '0';
            }
        }
        while (ascii_is_digit(c = read_char(pp)))
        {
            addText(scanner, c);
        }
        if (c == '.')
        {
            addText(scanner, c);
            uint8_t dbl_flg = 0;
            while (ascii_is_digit(c = read_char(pp)))
            {
                dbl_flg = 1;
                addText(scanner, c);
            }
            if (dbl_flg)
            {
                /* Check for f/F suffix for float, d/D for double */
                if (c == 'f' || c == 'F')
                {
                    float f_value;
                    sscanf(scanner->yytext, "%f", &f_value);
                    yylval->fv = f_value;
                    return FLOAT_LITERAL;
                }
                if (c == 'd' || c == 'D')
                {
                    double d_value;
                    sscanf(scanner->yytext, "%lf", &d_value);
                    yylval->dv = d_value;
                    return DOUBLE_LITERAL;
                }
                pushback_char(pp, c);
                double d_value;
                sscanf(scanner->yytext, "%lf", &d_value);
                yylval->dv = d_value;
                return DOUBLE_LITERAL;
            }
            else
            {
                fprintf(stderr, "double error\n");
                exit(1);
            }
        }
        else
        {
            /* Check for U/u and/or L/l suffix */
            int is_unsigned = 0;
            int is_long = 0;
            if (c == 'U' || c == 'u')
            {
                is_unsigned = 1;
                c = read_char(pp);
            }
            if (c == 'L' || c == 'l')
            {
                is_long = 1;
                c = read_char(pp);
            }
            /* Handle LU order as well (e.g., 123LU) */
            if (!is_unsigned && (c == 'U' || c == 'u'))
            {
                is_unsigned = 1;
                c = read_char(pp);
            }
            pushback_char(pp, c);

            /* Always parse as long first to detect overflow */
            long l_value;
            sscanf(scanner->yytext, "%ld", &l_value);

            if (is_long)
            {
                yylval->lv = l_value;
                return is_unsigned ? ULONG_LITERAL : LONG_LITERAL;
            }
            else if (is_unsigned)
            {
                /* Unsigned int: 0 to 4294967295 */
                if (l_value >= 0 && l_value <= 4294967295L)
                {
                    yylval->iv = (int)l_value;
                    return UINT_LITERAL;
                }
                else
                {
                    /* Promote to unsigned long */
                    yylval->lv = l_value;
                    return ULONG_LITERAL;
                }
            }
            else
            {
                /* Signed int: -2147483648 to 2147483647 */
                /* Note: We only see positive values here (minus is parsed separately) */
                if (l_value <= 2147483647L)
                {
                    yylval->iv = (int)l_value;
                    return INT_LITERAL;
                }
                else
                {
                    /* Promote to long (e.g., 2147483648 for -2147483648) */
                    yylval->lv = l_value;
                    return LONG_LITERAL;
                }
            }
        }
        break;
    }
    case ';':
    {
        return SEMICOLON;
    }
    case ':':
    {
        return COLON;
    }
    case '(':
    {
        return LP;
    }
    case ')':
    {
        return RP;
    }
    case '{':
    {
        return LC;
    }
    case '}':
    {
        return RC;
    }
    case ',':
    {
        return COMMA;
    }
    case '[':
    {
        c = read_char(pp);
        if (c == '[')
        {
            char *attr_text = read_balanced_attribute(pp);
            if (yylval)
            {
                yylval->name = attr_text ? cs_create_identifier(attr_text) : NULL;
            }
            free(attr_text);
            return ATTRIBUTE;
        }
        pushback_char(pp, c);
        return LBRACKET;
    }
    case ']':
    {
        return RBRACKET;
    }
    case '&':
    {
        c = read_char(pp);
        if (c == '&')
        {
            return LOGICAL_AND;
        }
        else if (c == '=')
        {
            return AND_ASSIGN_T;
        }
        pushback_char(pp, c);
        return BIT_AND;
    }
    case '|':
    {
        c = read_char(pp);
        if (c == '|')
        {
            return LOGICAL_OR;
        }
        else if (c == '=')
        {
            return OR_ASSIGN_T;
        }
        pushback_char(pp, c);
        return BIT_OR;
    }
    case '^':
    {
        c = read_char(pp);
        if (c == '=')
        {
            return XOR_ASSIGN_T;
        }
        pushback_char(pp, c);
        return BIT_XOR;
    }
    case '~':
    {
        return TILDE;
    }
    case '=':
    {
        if ((c = read_char(pp)) == '=')
        {
            return EQ;
        }
        else
        {
            pushback_char(pp, c);
            return ASSIGN_T;
        }
    }
    case '!':
    {
        if ((c = read_char(pp)) == '=')
        {
            return NE;
        }
        else
        {
            pushback_char(pp, c);
            return EXCLAMATION;
        }
    }
    case '>':
    {
        c = read_char(pp);
        if (c == '=')
        {
            return GE;
        }
        else if (c == '>')
        {
            int next = read_char(pp);
            if (next == '=')
            {
                return RSHIFT_ASSIGN_T;
            }
            pushback_char(pp, next);
            return RSHIFT;
        }
        else
        {
            pushback_char(pp, c);
            return GT;
        }
    }
    case '<':
    {
        c = read_char(pp);
        if (c == '=')
        {
            return LE;
        }
        else if (c == '<')
        {
            int next = read_char(pp);
            if (next == '=')
            {
                return LSHIFT_ASSIGN_T;
            }
            pushback_char(pp, next);
            return LSHIFT;
        }
        else
        {
            pushback_char(pp, c);
            return LT;
        }
    }
    case '+':
    {
        c = read_char(pp);
        if (c == '+')
        {
            return INCREMENT;
        }
        else if (c == '=')
        {
            return ADD_ASSIGN_T;
        }
        else
        {
            pushback_char(pp, c);
            return ADD;
        }
    }
    case '-':
    {
        c = read_char(pp);
        if (c == '-')
        {
            return DECREMENT;
        }
        else if (c == '=')
        {
            return SUB_ASSIGN_T;
        }
        else if (c == '>')
        {
            return ARROW;
        }
        else
        {
            pushback_char(pp, c);
            return SUB;
        }
    }
    case '*':
    {
        if ((c = read_char(pp)) == '=')
        {
            return MUL_ASSIGN_T;
        }
        else
        {
            pushback_char(pp, c);
            return MUL;
        }
    }
    case '/':
    {
        if ((c = read_char(pp)) == '=')
        {
            return DIV_ASSIGN_T;
        }
        else
        {
            pushback_char(pp, c);
            return DIV;
        }
    }
    case '%':
    {
        if ((c = read_char(pp)) == '=')
        {
            return MOD_ASSIGN_T;
        }
        else
        {
            pushback_char(pp, c);
            return MOD;
        }
    }
    case '.':
    {
        c = read_char(pp);
        if (c == '.')
        {
            int next = read_char(pp);
            if (next == '.')
            {
                return ELLIPSIS;
            }
            pushback_char(pp, next);
        }
        pushback_char(pp, c);
        return DOT;
    }
    case '?':
    {
        return QUESTION;
    }
    case '\'':
    {
        int value;
        c = read_char(pp);
        if (c == '\\')
        {
            int esc = read_char(pp);
            if (esc == EOF || esc == '\n')
            {
                fprintf(stderr, "unterminated character literal\\n");
                exit(1);
            }
            switch (esc)
            {
            case 'n':
                value = '\n';
                break;
            case 't':
                value = '\t';
                break;
            case '\\':
                value = '\\';
                break;
            case '\'':
                value = '\'';
                break;
            case '0':
                value = '\0';
                break;
            default:
                value = esc;
                break;
            }
        }
        else if (c == EOF || c == '\n')
        {
            fprintf(stderr, "unterminated character literal\\n");
            exit(1);
        }
        else
        {
            value = c;
        }

        int closing = read_char(pp);
        if (closing != '\'')
        {
            fprintf(stderr, "unterminated character literal\\n");
            exit(1);
        }
        yylval->iv = value;
        return INT_LITERAL;
    }
    case '"':
    {
        while (1)
        {
            c = read_char(pp);
            if (c == EOF || c == '\n')
            {
                fprintf(stderr, "unterminated string literal\n");
                exit(1);
            }
            if (c == '\\')
            {
                int next = read_char(pp);
                if (next == EOF)
                {
                    fprintf(stderr, "unterminated string literal\n");
                    exit(1);
                }
                switch (next)
                {
                case 'n':
                    addText(scanner, '\n');
                    break;
                case 't':
                    addText(scanner, '\t');
                    break;
                case 'r':
                    addText(scanner, '\r');
                    break;
                case '0':
                    addText(scanner, '\0');
                    break;
                case 'x':
                {
                    /* \xNN hex escape */
                    int h1 = read_char(pp);
                    int h2 = read_char(pp);
                    if (h1 == EOF || h2 == EOF)
                    {
                        fprintf(stderr, "incomplete hex escape\n");
                        exit(1);
                    }
                    int val = 0;
                    if (h1 >= '0' && h1 <= '9')
                        val = (h1 - '0') << 4;
                    else if (h1 >= 'a' && h1 <= 'f')
                        val = (h1 - 'a' + 10) << 4;
                    else if (h1 >= 'A' && h1 <= 'F')
                        val = (h1 - 'A' + 10) << 4;
                    if (h2 >= '0' && h2 <= '9')
                        val |= (h2 - '0');
                    else if (h2 >= 'a' && h2 <= 'f')
                        val |= (h2 - 'a' + 10);
                    else if (h2 >= 'A' && h2 <= 'F')
                        val |= (h2 - 'A' + 10);
                    addText(scanner, (char)val);
                    break;
                }
                case '\\':
                case '"':
                case '\'':
                    addText(scanner, (char)next);
                    break;
                default:
                    addText(scanner, '\\');
                    addText(scanner, (char)next);
                    break;
                }
                continue;
            }
            if (c == '"')
            {
                yylval->str.len = scanner->ytp;
                yylval->str.data = (uint8_t *)calloc(scanner->ytp, sizeof(uint8_t));
                memcpy(yylval->str.data, scanner->yytext, scanner->ytp);
                return STRING_LITERAL;
            }
            addText(scanner, c);
        }
    }
    case EOF:
    {
        return EOF;
    }
    default:
    {
        if (!ascii_is_identchar(c))
        {
            addText(scanner, c);
            error(scanner);
        }
        break;
    }
    }

    while (ascii_is_identchar(c))
    {
        addText(scanner, c);
        c = read_char(pp);
    }
    pushback_char(pp, c);

    if (try_expand_macro(pp, scanner->yytext))
    {
        resetText(scanner);
        goto retry;
    }

    struct OPE *op = in_word_set(scanner->yytext, strlen(scanner->yytext));
    if (op != NULL)
    {
        return op->type;
    }

    yylval->name = cs_create_identifier(scanner->yytext);

    /* All identifiers are now IDENTIFIER - type resolution is done in parser/semantic phase
     * using side-effect-only expression statements to disambiguate declarations */
    return IDENTIFIER;
}

int pp_next_token(Preprocessor *pp, YYSTYPE *yylval)
{
    return scan_token(pp, yylval);
}

const char *pp_current_text(Preprocessor *pp)
{
    return pp && pp->scanner ? pp->scanner->yytext : NULL;
}

int pp_current_line(Preprocessor *pp)
{
    return pp && pp->scanner ? pp->scanner->creator->line_number : 0;
}

void pp_get_token_location(Preprocessor *pp, const char **path, int *line)
{
    if (path)
    {
        *path = pp ? pp->token_path : NULL;
    }
    if (line)
    {
        *line = pp ? pp->token_line : 0;
    }
}
