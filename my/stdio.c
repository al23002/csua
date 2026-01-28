#include "stdio.h"

#include <stdlib.h>
#include <stdarg.h>

/* Java System streams */
static void *getSystemOut [[cminor::get_static("java/lang/System", "out", "Ljava/io/PrintStream;")]] () {}
static void *getSystemErr [[cminor::get_static("java/lang/System", "err", "Ljava/io/PrintStream;")]] () {}

/* Global stream instances */
FILE *stdout = NULL;
FILE *stderr = NULL;

/* Class initializer - runs once when class is loaded */
static void initStdio [[cminor::clinit]] ()
{
    stdout = (FILE *)calloc(1, sizeof(FILE));
    stdout->stream = getSystemOut();
    stderr = (FILE *)calloc(1, sizeof(FILE));
    stderr->stream = getSystemErr();
}

/* Get StandardCharsets.UTF_8 */
static void *getUTF8 [[cminor::get_static("java/nio/charset/StandardCharsets", "UTF_8", "Ljava/nio/charset/Charset;")]] () {}

/* File existence check */
static void *allocFile [[cminor::new("java/io/File")]] () {}
static void initFile [[cminor::invoke_special("java/io/File", "<init>", "(Ljava/lang/String;)V")]] (void *obj, void *filename) {}
static bool fileExists [[cminor::invoke_virtual("java/io/File", "exists", "()Z")]] (void *file) {}

/* FileInputStream for reading */
static void *allocFileInputStream [[cminor::new("java/io/FileInputStream")]] () {}
static void initFileInputStream [[cminor::invoke_special("java/io/FileInputStream", "<init>", "(Ljava/lang/String;)V")]] (void *obj, void *filename) {}
static int fisRead [[cminor::invoke_virtual("java/io/FileInputStream", "read", "([BII)I")]] (void *fis, char buf[], int off, int len) {}

/* FileOutputStream for writing */
static void *allocFileOutputStream [[cminor::new("java/io/FileOutputStream")]] () {}
static void initFileOutputStream [[cminor::invoke_special("java/io/FileOutputStream", "<init>", "(Ljava/lang/String;Z)V")]] (void *obj, void *filename, int append) {}
static void fosWrite [[cminor::invoke_virtual("java/io/FileOutputStream", "write", "([BII)V")]] (void *fos, char buf[], int off, int len) {}

/* Close via Closeable interface (works for both) */
static void streamClose [[cminor::invoke_interface("java/io/Closeable", "close", "()V")]] (void *stream) {}

/* Allocate String object */
static void *allocString [[cminor::new("java/lang/String")]] () {}

/* String constructor: String(byte[], int offset, int len, Charset) */
static void initStringBytesArr [[cminor::invoke_special("java/lang/String", "<init>", "([BIILjava/nio/charset/Charset;)V")]] (void *obj, char bytes[], int offset, int len, void *charset) {}

/* print without newline (internal) */
static void stdio_printString [[cminor::invoke_virtual("java/io/PrintStream", "print", "(Ljava/lang/String;)V")]] (void *stream, void *str) {}
static void stdio_printInt [[cminor::invoke_virtual("java/io/PrintStream", "print", "(I)V")]] (void *stream, int value) {}

/* Object.toString() for %p */
static void *objectToString [[cminor::invoke_virtual("java/lang/Object", "toString", "()Ljava/lang/String;")]] (void *obj) {}

/* Flush buffered text as String (for char[]) */
static void flush_buf(void *stream, char buf[], int start, int len)
{
    if (len <= 0)
        return;
    void *charset = getUTF8();
    void *str = allocString();
    initStringBytesArr(str, buf, start, len, charset);
    stdio_printString(stream, str);
    return;
}

/* Print char* by copying to VLA (no reflection needed) */
static void print_charptr(void *stream, char *s)
{
    int slen = 0;
    while (s[slen] != '\0')
        slen = slen + 1;
    char sbuf[slen];
    int j = 0;
    while (j < slen)
    {
        sbuf[j] = s[j];
        j = j + 1;
    }
    flush_buf(stream, sbuf, 0, slen);
    return;
}

/* Convert char* to Java String */
static void *charptr_to_jstring(const char *s)
{
    int slen = 0;
    while (s[slen] != '\0')
        slen = slen + 1;
    char sbuf[slen];
    int j = 0;
    while (j < slen)
    {
        sbuf[j] = s[j];
        j = j + 1;
    }
    void *charset = getUTF8();
    void *str = allocString();
    initStringBytesArr(str, sbuf, 0, slen, charset);
    return str;
}

int vfprintf(FILE *file, const char *fmt, va_list ap)
{
    int n = 0;
    while (fmt[n] != '\0')
        n++;

    /* VLA for buffering non-format chars */
    char buf[n + 1];
    int buf_pos = 0;
    int total = 0;

    void *out = file->stream;

    int i = 0;
    while (i < n)
    {
        if (fmt[i] == '%' && i + 1 < n)
        {
            char spec = fmt[i + 1];
            if (spec == 'd')
            {
                /* Flush buffer, then print int */
                flush_buf(out, buf, 0, buf_pos);
                total = total + buf_pos;
                buf_pos = 0;
                int val = va_arg(ap, int);
                stdio_printInt(out, val);
                i = i + 2;
            }
            else if (spec == 's')
            {
                /* Flush buffer, then print string */
                flush_buf(out, buf, 0, buf_pos);
                total = total + buf_pos;
                buf_pos = 0;
                char *s = va_arg(ap, char *);
                /* Print char* using helper */
                print_charptr(out, s);
                i = i + 2;
            }
            else if (spec == 'p')
            {
                /* Flush buffer, then print object.toString() */
                flush_buf(out, buf, 0, buf_pos);
                total = total + buf_pos;
                buf_pos = 0;
                void *obj = va_arg(ap, void *);
                void *str = objectToString(obj);
                stdio_printString(out, str);
                i = i + 2;
            }
            else if (spec == '%')
            {
                /* %% -> % */
                buf[buf_pos] = '%';
                buf_pos = buf_pos + 1;
                i = i + 2;
            }
            else
            {
                /* Unknown specifier, copy as-is */
                buf[buf_pos] = fmt[i];
                buf_pos = buf_pos + 1;
                i = i + 1;
            }
        }
        else
        {
            buf[buf_pos] = fmt[i];
            buf_pos = buf_pos + 1;
            i = i + 1;
        }
    }

    /* Flush remaining buffer */
    flush_buf(out, buf, 0, buf_pos);
    total = total + buf_pos;

    return total;
}

int vprintf(const char *fmt, va_list ap)
{
    return vfprintf(stdout, fmt, ap);
}

int fprintf(FILE *file, const char *fmt, ...)
{
    va_list ap;
    va_start(ap);
    int result = vfprintf(file, fmt, ap);
    va_end(ap);
    return result;
}

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap);
    int result = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return result;
}

int fflush(FILE *file)
{
    return 0;
}

int vsnprintf(char *str, int size, const char *fmt, va_list ap)
{
    if (size <= 0)
        return 0;

    int n = 0;
    while (fmt[n] != '\0')
        n++;

    int out_pos = 0;
    int i = 0;
    while (i < n && out_pos < size - 1)
    {
        if (fmt[i] == '%' && i + 1 < n)
        {
            char spec = fmt[i + 1];
            if (spec == 's')
            {
                char *s = va_arg(ap, char *);
                int j = 0;
                while (s[j] != '\0' && out_pos < size - 1)
                {
                    str[out_pos] = s[j];
                    out_pos = out_pos + 1;
                    j = j + 1;
                }
                i = i + 2;
            }
            else if (spec == 'd')
            {
                int val = va_arg(ap, int);
                /* Simple int to string conversion */
                char numbuf[32];
                int npos = 0;
                int neg = 0;
                if (val < 0)
                {
                    neg = 1;
                    val = -val;
                }
                if (val == 0)
                {
                    numbuf[npos] = '0';
                    npos = npos + 1;
                }
                else
                {
                    while (val > 0)
                    {
                        numbuf[npos] = (char)('0' + (val % 10));
                        npos = npos + 1;
                        val = val / 10;
                    }
                }
                /* Output in reverse */
                if (neg && out_pos < size - 1)
                {
                    str[out_pos] = '-';
                    out_pos = out_pos + 1;
                }
                int k = npos - 1;
                while (k >= 0 && out_pos < size - 1)
                {
                    str[out_pos] = numbuf[k];
                    out_pos = out_pos + 1;
                    k = k - 1;
                }
                i = i + 2;
            }
            else if (spec == '%')
            {
                str[out_pos] = '%';
                out_pos = out_pos + 1;
                i = i + 2;
            }
            else
            {
                str[out_pos] = fmt[i];
                out_pos = out_pos + 1;
                i = i + 1;
            }
        }
        else
        {
            str[out_pos] = fmt[i];
            out_pos = out_pos + 1;
            i = i + 1;
        }
    }

    str[out_pos] = '\0';
    return out_pos;
}

int snprintf(char *str, int size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap);
    int result = vsnprintf(str, size, fmt, ap);
    va_end(ap);
    return result;
}

int sscanf(const char *str, const char *fmt, ...)
{
    va_list ap;
    va_start(ap);

    int count = 0;
    int fi = 0;
    int si = 0;

    while (fmt[fi] != '\0')
    {
        if (fmt[fi] == '%')
        {
            fi = fi + 1;
            if (fmt[fi] == '\0')
                break;

            /* Check for 'l' modifier */
            int is_long = 0;
            if (fmt[fi] == 'l')
            {
                is_long = 1;
                fi = fi + 1;
            }

            char spec = fmt[fi];
            fi = fi + 1;

            char *endptr = NULL;

            if (spec == 'd')
            {
                /* %d or %ld - decimal integer */
                long val = strtol(str + si, &endptr, 10);
                if (endptr == str + si)
                    break;
                si = (int)(endptr - str);

                if (is_long)
                {
                    long *p = va_arg(ap, long *);
                    *p = val;
                }
                else
                {
                    int *p = va_arg(ap, int *);
                    *p = (int)val;
                }
                count = count + 1;
            }
            else if (spec == 'x')
            {
                /* %x or %lx - hex integer */
                long val = strtol(str + si, &endptr, 16);
                if (endptr == str + si)
                    break;
                si = (int)(endptr - str);

                if (is_long)
                {
                    long *p = va_arg(ap, long *);
                    *p = val;
                }
                else
                {
                    int *p = va_arg(ap, int *);
                    *p = (int)val;
                }
                count = count + 1;
            }
            else if (spec == 'f')
            {
                /* %f or %lf - floating point */
                if (is_long)
                {
                    double val = strtod(str + si, &endptr);
                    if (endptr == str + si)
                        break;
                    si = (int)(endptr - str);

                    double *p = va_arg(ap, double *);
                    *p = val;
                }
                else
                {
                    float val = strtof(str + si, &endptr);
                    if (endptr == str + si)
                        break;
                    si = (int)(endptr - str);

                    float *p = va_arg(ap, float *);
                    *p = val;
                }
                count = count + 1;
            }
        }
        else if (fmt[fi] == ' ' || fmt[fi] == '\t' || fmt[fi] == '\n')
        {
            /* Whitespace in format matches any whitespace in input */
            fi = fi + 1;
            while (str[si] == ' ' || str[si] == '\t' || str[si] == '\n')
                si = si + 1;
        }
        else
        {
            /* Literal character must match */
            if (str[si] != fmt[fi])
                break;
            fi = fi + 1;
            si = si + 1;
        }
    }

    va_end(ap);
    return count;
}

static FILE *fopen_read(const char *filename)
{
    void *jfilename = charptr_to_jstring(filename);
    /* Check if file exists before opening */
    void *jfile = allocFile();
    initFile(jfile, jfilename);
    if (!fileExists(jfile))
    {
        return NULL;
    }
    void *stream = allocFileInputStream();
    initFileInputStream(stream, jfilename);
    FILE *f = (FILE *)calloc(1, sizeof(FILE));
    f->stream = stream;
    return f;
}

static FILE *fopen_write(const char *filename, int append)
{
    void *jfilename = charptr_to_jstring(filename);
    void *stream = allocFileOutputStream();
    initFileOutputStream(stream, jfilename, append);
    FILE *f = (FILE *)calloc(1, sizeof(FILE));
    f->stream = stream;
    return f;
}

FILE *fopen(const char *filename, const char *mode)
{
    if (mode[0] == 'r')
    {
        return fopen_read(filename);
    }
    int append = (mode[0] == 'a') ? 1 : 0;
    return fopen_write(filename, append);
}

int fwrite(const char *ptr, int size, int count, FILE *file)
{
    int total = size * count;
    if (total <= 0)
        return 0;

    /* Copy to VLA for Java array */
    char buf[total];
    int i = 0;
    while (i < total)
    {
        buf[i] = ptr[i];
        i = i + 1;
    }

    fosWrite(file->stream, buf, 0, total);
    return count;
}

int fread(char *ptr, int size, int count, FILE *file)
{
    int total = size * count;
    if (total <= 0)
        return 0;

    /* Read into VLA then copy to ptr */
    char buf[total];
    int bytes_read = fisRead(file->stream, buf, 0, total);

    if (bytes_read <= 0)
        return 0;

    int i = 0;
    while (i < bytes_read)
    {
        ptr[i] = buf[i];
        i = i + 1;
    }

    return bytes_read / size;
}

int fclose(FILE *file)
{
    streamClose(file->stream);
    return 0;
}
