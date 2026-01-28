#include "ascii.h"

int ascii_is_ascii(char c)
{
    int uc = c & 0xFF;
    return uc < 128;
}

int ascii_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
           c == '\v' || c == '\f';
}

int ascii_is_digit(char c)
{
    return c >= '0' && c <= '9';
}

int ascii_is_alpha(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

int ascii_is_alnum(char c)
{
    return ascii_is_alpha(c) || ascii_is_digit(c);
}

int ascii_is_identchar(char c)
{
    return ascii_is_alnum(c) || c == '_';
}
