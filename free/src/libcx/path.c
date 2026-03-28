/*
 * path.c - Path manipulation utilities.
 * Part of libcx. Pure C89.
 */

#include "cx_path.h"
#include <stdlib.h>
#include <string.h>

char *cx_path_join(const char *a, const char *b)
{
    size_t alen, blen, total;
    char *result;
    int need_sep;

    alen = strlen(a);
    blen = strlen(b);

    /* Skip joining if b is absolute */
    if (blen > 0 && b[0] == '/') {
        result = (char *)malloc(blen + 1);
        memcpy(result, b, blen + 1);
        return result;
    }

    need_sep = (alen > 0 && a[alen - 1] != '/') ? 1 : 0;
    total = alen + need_sep + blen + 1;
    result = (char *)malloc(total);

    memcpy(result, a, alen);
    if (need_sep) {
        result[alen] = '/';
    }
    memcpy(result + alen + need_sep, b, blen + 1);
    return result;
}

char *cx_path_dirname(const char *path)
{
    const char *last_slash;
    size_t len;
    char *result;

    last_slash = strrchr(path, '/');
    if (!last_slash) {
        result = (char *)malloc(2);
        result[0] = '.';
        result[1] = '\0';
        return result;
    }

    if (last_slash == path) {
        result = (char *)malloc(2);
        result[0] = '/';
        result[1] = '\0';
        return result;
    }

    len = (size_t)(last_slash - path);
    result = (char *)malloc(len + 1);
    memcpy(result, path, len);
    result[len] = '\0';
    return result;
}

char *cx_path_basename(const char *path)
{
    const char *last_slash;
    const char *base;
    size_t len;
    char *result;

    last_slash = strrchr(path, '/');
    base = last_slash ? last_slash + 1 : path;
    len = strlen(base);

    result = (char *)malloc(len + 1);
    memcpy(result, base, len + 1);
    return result;
}

const char *cx_path_ext(const char *path)
{
    const char *base;
    const char *dot;

    base = strrchr(path, '/');
    if (!base) base = path;
    else base++;

    dot = strrchr(base, '.');
    if (!dot || dot == base) return "";
    return dot;
}

char *cx_path_normalize(const char *path)
{
    char *result;
    char *parts[256];
    int nparts;
    int i;
    size_t len;
    const char *p;
    char *out;
    int is_abs;
    char seg[256];
    int seglen;

    len = strlen(path);
    if (len == 0) {
        result = (char *)malloc(2);
        result[0] = '.';
        result[1] = '\0';
        return result;
    }

    is_abs = (path[0] == '/') ? 1 : 0;
    nparts = 0;
    p = path;

    while (*p) {
        /* Skip slashes */
        while (*p == '/') p++;
        if (*p == '\0') break;

        /* Read segment */
        seglen = 0;
        while (*p && *p != '/' && seglen < 255) {
            seg[seglen++] = *p++;
        }
        seg[seglen] = '\0';

        if (seglen == 1 && seg[0] == '.') {
            /* Skip "." */
            continue;
        } else if (seglen == 2 && seg[0] == '.' && seg[1] == '.') {
            if (nparts > 0 && strcmp(parts[nparts - 1], "..") != 0) {
                nparts--;
                free(parts[nparts]);
            } else if (!is_abs) {
                parts[nparts] = (char *)malloc(3);
                memcpy(parts[nparts], "..", 3);
                nparts++;
            }
        } else {
            parts[nparts] = (char *)malloc((size_t)(seglen + 1));
            memcpy(parts[nparts], seg, (size_t)(seglen + 1));
            nparts++;
        }
    }

    /* Build result */
    if (nparts == 0) {
        if (is_abs) {
            result = (char *)malloc(2);
            result[0] = '/';
            result[1] = '\0';
        } else {
            result = (char *)malloc(2);
            result[0] = '.';
            result[1] = '\0';
        }
        return result;
    }

    /* Calculate total length */
    len = is_abs ? 1 : 0;
    for (i = 0; i < nparts; i++) {
        if (i > 0) len++;
        len += strlen(parts[i]);
    }

    result = (char *)malloc(len + 1);
    out = result;
    if (is_abs) {
        *out++ = '/';
    }
    for (i = 0; i < nparts; i++) {
        size_t plen;
        if (i > 0) *out++ = '/';
        plen = strlen(parts[i]);
        memcpy(out, parts[i], plen);
        out += plen;
        free(parts[i]);
    }
    *out = '\0';
    return result;
}

int cx_path_is_abs(const char *path)
{
    return path[0] == '/';
}
