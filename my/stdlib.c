#include "stdlib.h"

/* Java System.exit */
static void systemExit [[cminor::invoke_static("java/lang/System", "exit", "(I)V")]] (int status) {}

/* free - no-op, GC handles memory */
void free(void *ptr)
{
    (void)ptr;
}

/* exit - calls System.exit */
void exit(int status)
{
    systemExit(status);
}

/* Get StandardCharsets.UTF_8 */
static void *getUTF8 [[cminor::get_static("java/nio/charset/StandardCharsets", "UTF_8", "Ljava/nio/charset/Charset;")]] () {}

/* Allocate String object */
static void *allocString [[cminor::new("java/lang/String")]] () {}

/* String constructor: String(byte[], int offset, int len, Charset) */
static void initStringBytesVoid [[cminor::invoke_special("java/lang/String", "<init>", "([BIILjava/nio/charset/Charset;)V")]] (void *obj, void *bytes, int offset, int len, void *charset) {}

/* Extract base byte[] from __charPtr */
static void *getCharPtrBase [[cminor::get_field("__charPtr", "base", "[B")]] (const char *ptr) {}

/* Extract offset from __charPtr */
static int getCharPtrOffset [[cminor::get_field("__charPtr", "offset", "I")]] (const char *ptr) {}

/* Long.parseLong(String, radix) */
static long parseLongRadix [[cminor::invoke_static("java/lang/Long", "parseLong", "(Ljava/lang/String;I)J")]] (void *s, int radix) {}

/* Helper: check if char is hex digit */
static int is_hex_digit(char c)
{
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

/* Helper: create Java String from char* range */
static void *make_jstring(const char *str, int start, int len)
{
    void *bytes = getCharPtrBase(str);
    int baseOffset = getCharPtrOffset(str);
    void *charset = getUTF8();
    void *jstr = allocString();
    initStringBytesVoid(jstr, bytes, baseOffset + start, len, charset);
    return jstr;
}

/* strtol - parse integer from string using Long.parseLong */
long strtol(const char *str, char **endptr, int base)
{
    int i = 0;

    /* Skip leading whitespace */
    while (str[i] == ' ' || str[i] == '\t' || str[i] == '\n')
        i = i + 1;

    int start = i;
    int sign = 1;

    /* Check for sign */
    if (str[i] == '-')
    {
        sign = -1;
        i = i + 1;
    }
    else if (str[i] == '+')
    {
        i = i + 1;
    }

    int num_start = i;

    /* Handle 0x prefix for base 16 */
    if (base == 16 && str[i] == '0' && (str[i + 1] == 'x' || str[i + 1] == 'X'))
    {
        i = i + 2;
        num_start = i;
    }

    if (base == 16)
    {
        /* Check for at least one hex digit */
        if (!is_hex_digit(str[i]))
        {
            if (endptr != NULL)
                *endptr = (char *)(str + start);
            return 0L;
        }

        /* Count hex digits */
        while (is_hex_digit(str[i]))
            i = i + 1;
    }
    else
    {
        /* Base 10: check for at least one digit */
        if (str[i] < '0' || str[i] > '9')
        {
            if (endptr != NULL)
                *endptr = (char *)(str + start);
            return 0L;
        }

        /* Count digits */
        while (str[i] >= '0' && str[i] <= '9')
            i = i + 1;
    }

    /* Set endptr to position after last digit */
    if (endptr != NULL)
        *endptr = (char *)(str + i);

    /* Build Java String from the number portion (without sign, without 0x) */
    void *jstr = make_jstring(str, num_start, i - num_start);

    long result = parseLongRadix(jstr, base);
    return sign * result;
}

/* Double.parseDouble(String) */
static double parseDouble [[cminor::invoke_static("java/lang/Double", "parseDouble", "(Ljava/lang/String;)D")]] (void *s) {}

/* Float.parseFloat(String) */
static float parseFloat [[cminor::invoke_static("java/lang/Float", "parseFloat", "(Ljava/lang/String;)F")]] (void *s) {}

/* strtod - parse double from string using Double.parseDouble */
double strtod(const char *str, char **endptr)
{
    int i = 0;

    /* Skip leading whitespace */
    while (str[i] == ' ' || str[i] == '\t' || str[i] == '\n')
        i = i + 1;

    int start = i;

    /* Check for sign */
    if (str[i] == '-' || str[i] == '+')
        i = i + 1;

    int has_digits = 0;

    /* Integer part */
    while (str[i] >= '0' && str[i] <= '9')
    {
        has_digits = 1;
        i = i + 1;
    }

    /* Decimal point and fractional part */
    if (str[i] == '.')
    {
        i = i + 1;
        while (str[i] >= '0' && str[i] <= '9')
        {
            has_digits = 1;
            i = i + 1;
        }
    }

    /* No digits found */
    if (!has_digits)
    {
        if (endptr != NULL)
            *endptr = (char *)(str + start);
        return 0.0;
    }

    /* Exponent part */
    if (str[i] == 'e' || str[i] == 'E')
    {
        int exp_start = i;
        i = i + 1;

        /* Optional sign */
        if (str[i] == '-' || str[i] == '+')
            i = i + 1;

        /* Must have at least one digit */
        if (str[i] >= '0' && str[i] <= '9')
        {
            while (str[i] >= '0' && str[i] <= '9')
                i = i + 1;
        }
        else
        {
            /* Invalid exponent, revert */
            i = exp_start;
        }
    }

    /* Set endptr to position after last valid char */
    if (endptr != NULL)
        *endptr = (char *)(str + i);

    /* Build Java String and parse */
    void *jstr = make_jstring(str, start, i - start);
    return parseDouble(jstr);
}

/* strtof - parse float from string using Float.parseFloat */
float strtof(const char *str, char **endptr)
{
    int i = 0;

    /* Skip leading whitespace */
    while (str[i] == ' ' || str[i] == '\t' || str[i] == '\n')
        i = i + 1;

    int start = i;

    /* Check for sign */
    if (str[i] == '-' || str[i] == '+')
        i = i + 1;

    int has_digits = 0;

    /* Integer part */
    while (str[i] >= '0' && str[i] <= '9')
    {
        has_digits = 1;
        i = i + 1;
    }

    /* Decimal point and fractional part */
    if (str[i] == '.')
    {
        i = i + 1;
        while (str[i] >= '0' && str[i] <= '9')
        {
            has_digits = 1;
            i = i + 1;
        }
    }

    /* No digits found */
    if (!has_digits)
    {
        if (endptr != NULL)
            *endptr = (char *)(str + start);
        return 0.0f;
    }

    /* Exponent part */
    if (str[i] == 'e' || str[i] == 'E')
    {
        int exp_start = i;
        i = i + 1;

        /* Optional sign */
        if (str[i] == '-' || str[i] == '+')
            i = i + 1;

        /* Must have at least one digit */
        if (str[i] >= '0' && str[i] <= '9')
        {
            while (str[i] >= '0' && str[i] <= '9')
                i = i + 1;
        }
        else
        {
            /* Invalid exponent, revert */
            i = exp_start;
        }
    }

    /* Set endptr to position after last valid char */
    if (endptr != NULL)
        *endptr = (char *)(str + i);

    /* Build Java String and parse */
    void *jstr = make_jstring(str, start, i - start);
    return parseFloat(jstr);
}
