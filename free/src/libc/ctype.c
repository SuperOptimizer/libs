/*
 * ctype.c - Character classification functions for the free libc.
 * Pure C89. No external dependencies.
 */

#include <ctype.h>

int isalpha(int c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

int isdigit(int c)
{
    return c >= '0' && c <= '9';
}

int isalnum(int c)
{
    return isalpha(c) || isdigit(c);
}

int isspace(int c)
{
    return c == ' ' || c == '\t' || c == '\n'
        || c == '\r' || c == '\f' || c == '\v';
}

int isupper(int c)
{
    return c >= 'A' && c <= 'Z';
}

int islower(int c)
{
    return c >= 'a' && c <= 'z';
}

int isprint(int c)
{
    return c >= 0x20 && c <= 0x7E;
}

int ispunct(int c)
{
    return isprint(c) && !isspace(c) && !isalnum(c);
}

int isxdigit(int c)
{
    return isdigit(c)
        || (c >= 'A' && c <= 'F')
        || (c >= 'a' && c <= 'f');
}

int toupper(int c)
{
    if (islower(c)) {
        return c - ('a' - 'A');
    }
    return c;
}

int tolower(int c)
{
    if (isupper(c)) {
        return c + ('a' - 'A');
    }
    return c;
}
