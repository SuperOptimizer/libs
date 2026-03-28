// json.h — Pure C23 JSON library
// Single-header: declare API, then #define JS_IMPLEMENTATION in one .c file.
#ifndef JSON_H
#define JSON_H

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// ── Version ─────────────────────────────────────────────────────────────────

#define JS_VERSION_MAJOR 0
#define JS_VERSION_MINOR 1
#define JS_VERSION_PATCH 0

// ── C23 Compat ──────────────────────────────────────────────────────────────

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
  #define JS_NODISCARD    [[nodiscard]]
  #define JS_MAYBE_UNUSED [[maybe_unused]]
#elif defined(__GNUC__) || defined(__clang__)
  #define JS_NODISCARD    __attribute__((warn_unused_result))
  #define JS_MAYBE_UNUSED __attribute__((unused))
#else
  #define JS_NODISCARD
  #define JS_MAYBE_UNUSED
#endif

// ── Linkage ─────────────────────────────────────────────────────────────────

#ifndef JSDEF
  #ifdef JS_STATIC
    #define JSDEF static
  #else
    #define JSDEF extern
  #endif
#endif

// ── Allocator Hooks ─────────────────────────────────────────────────────────

#ifndef JS_MALLOC
  #include <stdlib.h>
  #define JS_MALLOC(sz)       malloc(sz)
  #define JS_REALLOC(p, sz)   realloc(p, sz)
  #define JS_FREE(p)          free(p)
  #define JS_CALLOC(n, sz)    calloc(n, sz)
#endif

// ── Status Codes ────────────────────────────────────────────────────────────

typedef enum js_status {
    JS_OK = 0,
    JS_ERR_NULL_ARG,
    JS_ERR_ALLOC,
    JS_ERR_PARSE,
    JS_ERR_TYPE,
    JS_ERR_NOT_FOUND,
    JS_ERR_IO,
} js_status;

// ── Types ───────────────────────────────────────────────────────────────────

typedef enum js_type {
    JS_NULL,
    JS_BOOL,
    JS_INT,
    JS_FLOAT,
    JS_STRING,
    JS_ARRAY,
    JS_OBJECT,
} js_type;

typedef struct js_value js_value;

// ── Object Iterator ─────────────────────────────────────────────────────────

typedef struct js_object_iter {
    const js_value* obj;
    int              idx;
} js_object_iter;

// ── Lifecycle ───────────────────────────────────────────────────────────────

JS_NODISCARD JSDEF js_status js_parse(const char* str, js_value** out);
JS_NODISCARD JSDEF js_status js_parse_file(const char* path, js_value** out);
JSDEF void js_free(js_value* v);
JS_NODISCARD JSDEF js_status js_clone(js_value** out, const js_value* src);

// ── Creation ────────────────────────────────────────────────────────────────

JSDEF js_value* js_null(void);
JSDEF js_value* js_bool(bool val);
JSDEF js_value* js_int(int64_t val);
JSDEF js_value* js_float(double val);
JSDEF js_value* js_string(const char* val);
JSDEF js_value* js_array(void);
JSDEF js_value* js_object(void);

// ── Type Queries ────────────────────────────────────────────────────────────

JSDEF js_type js_typeof(const js_value* v);
JSDEF bool    js_is_null(const js_value* v);
JSDEF bool    js_is_bool(const js_value* v);
JSDEF bool    js_is_int(const js_value* v);
JSDEF bool    js_is_float(const js_value* v);
JSDEF bool    js_is_string(const js_value* v);
JSDEF bool    js_is_array(const js_value* v);
JSDEF bool    js_is_object(const js_value* v);

// ── Accessors ───────────────────────────────────────────────────────────────

JSDEF bool        js_get_bool(const js_value* v);
JSDEF int64_t     js_get_int(const js_value* v);
JSDEF double      js_get_float(const js_value* v);
JSDEF const char* js_get_string(const js_value* v);
JSDEF js_value*   js_get(const js_value* obj, const char* key);
JSDEF js_value*   js_at(const js_value* arr, int index);

// ── Mutation ────────────────────────────────────────────────────────────────

JS_NODISCARD JSDEF js_status js_set(js_value* obj, const char* key, js_value* val);
JS_NODISCARD JSDEF js_status js_push(js_value* arr, js_value* val);
JSDEF int js_array_len(const js_value* v);
JSDEF int js_object_len(const js_value* v);

// ── Serialization ───────────────────────────────────────────────────────────

JS_NODISCARD JSDEF js_status js_dump(const js_value* v, char** out, int indent);
JS_NODISCARD JSDEF js_status js_dump_file(const js_value* v, const char* path,
                                          int indent);

// ── Iteration ───────────────────────────────────────────────────────────────

JSDEF js_object_iter js_object_iter_init(const js_value* obj);
JSDEF bool           js_object_next(js_object_iter* it);
JSDEF const char*    js_object_key(const js_object_iter* it);
JSDEF js_value*      js_object_val(const js_object_iter* it);

// ── Utilities ───────────────────────────────────────────────────────────────

JSDEF const char* js_status_str(js_status s);
JSDEF const char* js_version_str(void);
JSDEF bool        js_contains(const js_value* obj, const char* key);

// ═══════════════════════════════════════════════════════════════════════════
// Implementation
// ═══════════════════════════════════════════════════════════════════════════

#ifdef JS_IMPLEMENTATION

#include <math.h>
#include <errno.h>

// ── Internal Value Definition ───────────────────────────────────────────────

typedef struct js_pair {
    char*     key;
    js_value* val;
} js_pair;

struct js_value {
    js_type type;
    union {
        bool        boolean;
        int64_t     integer;
        double      floating;
        char*       string;
        struct {
            js_value** items;
            int        len;
            int        cap;
        } array;
        struct {
            js_pair* pairs;
            int      len;
            int      cap;
        } object;
    } u;
};

// ── Internal Helpers ────────────────────────────────────────────────────────

static js_value* js__alloc_value(js_type type)
{
    js_value* v = (js_value*)JS_CALLOC(1, sizeof(js_value));
    if (v) v->type = type;
    return v;
}

static char* js__strdup(const char* s)
{
    if (!s) return NULL;
    size_t len = strlen(s);
    char* dup = (char*)JS_MALLOC(len + 1);
    if (dup) memcpy(dup, s, len + 1);
    return dup;
}

// ── Parser ──────────────────────────────────────────────────────────────────

typedef struct {
    const char* src;
    int         pos;
} js__parser;

static void js__skip_ws(js__parser* p)
{
    while (p->src[p->pos] == ' ' || p->src[p->pos] == '\t' ||
           p->src[p->pos] == '\n' || p->src[p->pos] == '\r')
        p->pos++;
}

static char js__peek(js__parser* p)
{
    js__skip_ws(p);
    return p->src[p->pos];
}

static char js__next(js__parser* p)
{
    return p->src[p->pos++];
}

static bool js__match(js__parser* p, const char* word)
{
    js__skip_ws(p);
    int len = (int)strlen(word);
    if (strncmp(p->src + p->pos, word, (size_t)len) == 0) {
        p->pos += len;
        return true;
    }
    return false;
}

static js_status js__parse_value(js__parser* p, js_value** out);

static js_status js__parse_string_raw(js__parser* p, char** out)
{
    if (js__next(p) != '"') return JS_ERR_PARSE;

    // First pass: compute length
    int start = p->pos;
    int len = 0;
    while (p->src[p->pos] && p->src[p->pos] != '"') {
        if (p->src[p->pos] == '\\') {
            p->pos++;
            if (p->src[p->pos] == 'u') {
                p->pos += 4;
                // UTF-8 encode: up to 3 bytes for BMP
                unsigned int cp = 0;
                for (int i = 0; i < 4; i++) {
                    // just count max size
                    (void)cp;
                }
                len += 3; // worst case BMP
            } else {
                len++;
            }
            p->pos++;
        } else {
            len++;
            p->pos++;
        }
    }
    if (p->src[p->pos] != '"') return JS_ERR_PARSE;

    // Reset and do second pass
    p->pos = start;
    char* buf = (char*)JS_MALLOC((size_t)(len + 1));
    if (!buf) return JS_ERR_ALLOC;

    int wi = 0;
    while (p->src[p->pos] && p->src[p->pos] != '"') {
        if (p->src[p->pos] == '\\') {
            p->pos++;
            char esc = p->src[p->pos++];
            switch (esc) {
                case '"':  buf[wi++] = '"';  break;
                case '\\': buf[wi++] = '\\'; break;
                case '/':  buf[wi++] = '/';  break;
                case 'b':  buf[wi++] = '\b'; break;
                case 'f':  buf[wi++] = '\f'; break;
                case 'n':  buf[wi++] = '\n'; break;
                case 'r':  buf[wi++] = '\r'; break;
                case 't':  buf[wi++] = '\t'; break;
                case 'u': {
                    unsigned int cp = 0;
                    for (int i = 0; i < 4; i++) {
                        char c = p->src[p->pos++];
                        cp <<= 4;
                        if (c >= '0' && c <= '9') cp |= (unsigned)(c - '0');
                        else if (c >= 'a' && c <= 'f') cp |= (unsigned)(c - 'a' + 10);
                        else if (c >= 'A' && c <= 'F') cp |= (unsigned)(c - 'A' + 10);
                        else { JS_FREE(buf); return JS_ERR_PARSE; }
                    }
                    if (cp < 0x80) {
                        buf[wi++] = (char)cp;
                    } else if (cp < 0x800) {
                        buf[wi++] = (char)(0xC0 | (cp >> 6));
                        buf[wi++] = (char)(0x80 | (cp & 0x3F));
                    } else {
                        buf[wi++] = (char)(0xE0 | (cp >> 12));
                        buf[wi++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                        buf[wi++] = (char)(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default: buf[wi++] = esc; break;
            }
        } else {
            buf[wi++] = p->src[p->pos++];
        }
    }
    buf[wi] = '\0';
    p->pos++; // skip closing quote
    *out = buf;
    return JS_OK;
}

static js_status js__parse_number(js__parser* p, js_value** out)
{
    const char* start = p->src + p->pos;
    char* end = NULL;
    bool is_float = false;

    // Scan to determine if int or float
    int scan = p->pos;
    if (p->src[scan] == '-') scan++;
    while (p->src[scan] >= '0' && p->src[scan] <= '9') scan++;
    if (p->src[scan] == '.' || p->src[scan] == 'e' || p->src[scan] == 'E')
        is_float = true;

    if (is_float) {
        double val = strtod(start, &end);
        if (end == start) return JS_ERR_PARSE;
        p->pos = (int)(end - p->src);
        js_value* v = js__alloc_value(JS_FLOAT);
        if (!v) return JS_ERR_ALLOC;
        v->u.floating = val;
        *out = v;
    } else {
        int64_t val = strtoll(start, &end, 10);
        if (end == start) return JS_ERR_PARSE;
        p->pos = (int)(end - p->src);
        js_value* v = js__alloc_value(JS_INT);
        if (!v) return JS_ERR_ALLOC;
        v->u.integer = val;
        *out = v;
    }
    return JS_OK;
}

static js_status js__parse_string(js__parser* p, js_value** out)
{
    char* str = NULL;
    js_status s = js__parse_string_raw(p, &str);
    if (s != JS_OK) return s;
    js_value* v = js__alloc_value(JS_STRING);
    if (!v) { JS_FREE(str); return JS_ERR_ALLOC; }
    v->u.string = str;
    *out = v;
    return JS_OK;
}

static js_status js__parse_array(js__parser* p, js_value** out)
{
    p->pos++; // skip '['
    js_value* arr = js__alloc_value(JS_ARRAY);
    if (!arr) return JS_ERR_ALLOC;
    arr->u.array.items = NULL;
    arr->u.array.len = 0;
    arr->u.array.cap = 0;

    if (js__peek(p) == ']') { p->pos++; *out = arr; return JS_OK; }

    for (;;) {
        js_value* elem = NULL;
        js_status s = js__parse_value(p, &elem);
        if (s != JS_OK) { js_free(arr); return s; }

        js_status ps = js_push(arr, elem);
        if (ps != JS_OK) { js_free(elem); js_free(arr); return ps; }

        char c = js__peek(p);
        if (c == ',') { p->pos++; continue; }
        if (c == ']') { p->pos++; break; }
        js_free(arr);
        return JS_ERR_PARSE;
    }
    *out = arr;
    return JS_OK;
}

static js_status js__parse_object(js__parser* p, js_value** out)
{
    p->pos++; // skip '{'
    js_value* obj = js__alloc_value(JS_OBJECT);
    if (!obj) return JS_ERR_ALLOC;
    obj->u.object.pairs = NULL;
    obj->u.object.len = 0;
    obj->u.object.cap = 0;

    if (js__peek(p) == '}') { p->pos++; *out = obj; return JS_OK; }

    for (;;) {
        js__skip_ws(p);
        char* key = NULL;
        js_status ks = js__parse_string_raw(p, &key);
        if (ks != JS_OK) { js_free(obj); return ks; }

        if (js__peek(p) != ':') { JS_FREE(key); js_free(obj); return JS_ERR_PARSE; }
        p->pos++;

        js_value* val = NULL;
        js_status vs = js__parse_value(p, &val);
        if (vs != JS_OK) { JS_FREE(key); js_free(obj); return vs; }

        js_status ss = js_set(obj, key, val);
        JS_FREE(key);
        if (ss != JS_OK) { js_free(val); js_free(obj); return ss; }

        char c = js__peek(p);
        if (c == ',') { p->pos++; continue; }
        if (c == '}') { p->pos++; break; }
        js_free(obj);
        return JS_ERR_PARSE;
    }
    *out = obj;
    return JS_OK;
}

static js_status js__parse_value(js__parser* p, js_value** out)
{
    char c = js__peek(p);
    if (c == '\0') return JS_ERR_PARSE;

    if (c == 'n' && js__match(p, "null")) {
        *out = js_null();
        return *out ? JS_OK : JS_ERR_ALLOC;
    }
    if (c == 't' && js__match(p, "true")) {
        *out = js_bool(true);
        return *out ? JS_OK : JS_ERR_ALLOC;
    }
    if (c == 'f' && js__match(p, "false")) {
        *out = js_bool(false);
        return *out ? JS_OK : JS_ERR_ALLOC;
    }
    if (c == '"') return js__parse_string(p, out);
    if (c == '[') return js__parse_array(p, out);
    if (c == '{') return js__parse_object(p, out);
    if (c == '-' || (c >= '0' && c <= '9')) return js__parse_number(p, out);

    return JS_ERR_PARSE;
}

// ── Serializer ──────────────────────────────────────────────────────────────

typedef struct {
    char*  buf;
    int    len;
    int    cap;
} js__strbuf;

static js_status js__sb_init(js__strbuf* sb)
{
    sb->cap = 256;
    sb->len = 0;
    sb->buf = (char*)JS_MALLOC((size_t)sb->cap);
    if (!sb->buf) return JS_ERR_ALLOC;
    sb->buf[0] = '\0';
    return JS_OK;
}

static js_status js__sb_grow(js__strbuf* sb, int need)
{
    if (sb->len + need + 1 <= sb->cap) return JS_OK;
    int newcap = sb->cap * 2;
    while (newcap < sb->len + need + 1) newcap *= 2;
    char* nb = (char*)JS_REALLOC(sb->buf, (size_t)newcap);
    if (!nb) return JS_ERR_ALLOC;
    sb->buf = nb;
    sb->cap = newcap;
    return JS_OK;
}

static js_status js__sb_append(js__strbuf* sb, const char* s, int slen)
{
    js_status st = js__sb_grow(sb, slen);
    if (st != JS_OK) return st;
    memcpy(sb->buf + sb->len, s, (size_t)slen);
    sb->len += slen;
    sb->buf[sb->len] = '\0';
    return JS_OK;
}

static js_status js__sb_puts(js__strbuf* sb, const char* s)
{
    return js__sb_append(sb, s, (int)strlen(s));
}

static js_status js__sb_putc(js__strbuf* sb, char c)
{
    return js__sb_append(sb, &c, 1);
}

static js_status js__sb_indent(js__strbuf* sb, int depth, int indent)
{
    if (indent <= 0) return JS_OK;
    int n = depth * indent;
    js_status st = js__sb_grow(sb, n);
    if (st != JS_OK) return st;
    for (int i = 0; i < n; i++) sb->buf[sb->len++] = ' ';
    sb->buf[sb->len] = '\0';
    return JS_OK;
}

static js_status js__dump_string(js__strbuf* sb, const char* s)
{
    js_status st = js__sb_putc(sb, '"');
    if (st != JS_OK) return st;
    for (const char* p = s; *p; p++) {
        switch (*p) {
            case '"':  st = js__sb_puts(sb, "\\\""); break;
            case '\\': st = js__sb_puts(sb, "\\\\"); break;
            case '\b': st = js__sb_puts(sb, "\\b");  break;
            case '\f': st = js__sb_puts(sb, "\\f");  break;
            case '\n': st = js__sb_puts(sb, "\\n");  break;
            case '\r': st = js__sb_puts(sb, "\\r");  break;
            case '\t': st = js__sb_puts(sb, "\\t");  break;
            default:
                if ((unsigned char)*p < 0x20) {
                    char esc[8];
                    snprintf(esc, sizeof(esc), "\\u%04x", (unsigned char)*p);
                    st = js__sb_puts(sb, esc);
                } else {
                    st = js__sb_putc(sb, *p);
                }
                break;
        }
        if (st != JS_OK) return st;
    }
    return js__sb_putc(sb, '"');
}

static js_status js__dump_value(js__strbuf* sb, const js_value* v,
                                int indent, int depth)
{
    if (!v) return js__sb_puts(sb, "null");

    switch (v->type) {
        case JS_NULL:
            return js__sb_puts(sb, "null");
        case JS_BOOL:
            return js__sb_puts(sb, v->u.boolean ? "true" : "false");
        case JS_INT: {
            char tmp[32];
            snprintf(tmp, sizeof(tmp), "%" PRId64, v->u.integer);
            return js__sb_puts(sb, tmp);
        }
        case JS_FLOAT: {
            char tmp[64];
            snprintf(tmp, sizeof(tmp), "%.17g", v->u.floating);
            // Ensure there's a decimal point or exponent
            if (!strchr(tmp, '.') && !strchr(tmp, 'e') && !strchr(tmp, 'E')) {
                size_t tl = strlen(tmp);
                tmp[tl] = '.';
                tmp[tl+1] = '0';
                tmp[tl+2] = '\0';
            }
            return js__sb_puts(sb, tmp);
        }
        case JS_STRING:
            return js__dump_string(sb, v->u.string);
        case JS_ARRAY: {
            js_status st = js__sb_putc(sb, '[');
            if (st != JS_OK) return st;
            for (int i = 0; i < v->u.array.len; i++) {
                if (i > 0) {
                    st = js__sb_putc(sb, ',');
                    if (st != JS_OK) return st;
                }
                if (indent > 0) {
                    st = js__sb_putc(sb, '\n');
                    if (st != JS_OK) return st;
                    st = js__sb_indent(sb, depth + 1, indent);
                    if (st != JS_OK) return st;
                }
                st = js__dump_value(sb, v->u.array.items[i], indent, depth + 1);
                if (st != JS_OK) return st;
            }
            if (indent > 0 && v->u.array.len > 0) {
                st = js__sb_putc(sb, '\n');
                if (st != JS_OK) return st;
                st = js__sb_indent(sb, depth, indent);
                if (st != JS_OK) return st;
            }
            return js__sb_putc(sb, ']');
        }
        case JS_OBJECT: {
            js_status st = js__sb_putc(sb, '{');
            if (st != JS_OK) return st;
            for (int i = 0; i < v->u.object.len; i++) {
                if (i > 0) {
                    st = js__sb_putc(sb, ',');
                    if (st != JS_OK) return st;
                }
                if (indent > 0) {
                    st = js__sb_putc(sb, '\n');
                    if (st != JS_OK) return st;
                    st = js__sb_indent(sb, depth + 1, indent);
                    if (st != JS_OK) return st;
                }
                st = js__dump_string(sb, v->u.object.pairs[i].key);
                if (st != JS_OK) return st;
                st = js__sb_putc(sb, ':');
                if (st != JS_OK) return st;
                if (indent > 0) {
                    st = js__sb_putc(sb, ' ');
                    if (st != JS_OK) return st;
                }
                st = js__dump_value(sb, v->u.object.pairs[i].val, indent, depth + 1);
                if (st != JS_OK) return st;
            }
            if (indent > 0 && v->u.object.len > 0) {
                st = js__sb_putc(sb, '\n');
                if (st != JS_OK) return st;
                st = js__sb_indent(sb, depth, indent);
                if (st != JS_OK) return st;
            }
            return js__sb_putc(sb, '}');
        }
    }
    return JS_ERR_TYPE;
}

// ── Lifecycle Implementation ────────────────────────────────────────────────

JSDEF js_status js_parse(const char* str, js_value** out)
{
    if (!str || !out) return JS_ERR_NULL_ARG;
    js__parser p = { .src = str, .pos = 0 };
    js_status s = js__parse_value(&p, out);
    if (s != JS_OK) return s;
    // Verify no trailing non-whitespace
    js__skip_ws(&p);
    if (p.src[p.pos] != '\0') { js_free(*out); *out = NULL; return JS_ERR_PARSE; }
    return JS_OK;
}

JSDEF js_status js_parse_file(const char* path, js_value** out)
{
    if (!path || !out) return JS_ERR_NULL_ARG;
    FILE* f = fopen(path, "rb");
    if (!f) return JS_ERR_IO;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz < 0) { fclose(f); return JS_ERR_IO; }

    char* buf = (char*)JS_MALLOC((size_t)sz + 1);
    if (!buf) { fclose(f); return JS_ERR_ALLOC; }

    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';

    js_status s = js_parse(buf, out);
    JS_FREE(buf);
    return s;
}

JSDEF void js_free(js_value* v)
{
    if (!v) return;
    switch (v->type) {
        case JS_STRING:
            JS_FREE(v->u.string);
            break;
        case JS_ARRAY:
            for (int i = 0; i < v->u.array.len; i++)
                js_free(v->u.array.items[i]);
            JS_FREE(v->u.array.items);
            break;
        case JS_OBJECT:
            for (int i = 0; i < v->u.object.len; i++) {
                JS_FREE(v->u.object.pairs[i].key);
                js_free(v->u.object.pairs[i].val);
            }
            JS_FREE(v->u.object.pairs);
            break;
        default:
            break;
    }
    JS_FREE(v);
}

JSDEF js_status js_clone(js_value** out, const js_value* src)
{
    if (!out) return JS_ERR_NULL_ARG;
    if (!src) { *out = NULL; return JS_OK; }

    switch (src->type) {
        case JS_NULL:
            *out = js_null();
            return *out ? JS_OK : JS_ERR_ALLOC;
        case JS_BOOL:
            *out = js_bool(src->u.boolean);
            return *out ? JS_OK : JS_ERR_ALLOC;
        case JS_INT:
            *out = js_int(src->u.integer);
            return *out ? JS_OK : JS_ERR_ALLOC;
        case JS_FLOAT:
            *out = js_float(src->u.floating);
            return *out ? JS_OK : JS_ERR_ALLOC;
        case JS_STRING:
            *out = js_string(src->u.string);
            return *out ? JS_OK : JS_ERR_ALLOC;
        case JS_ARRAY: {
            js_value* arr = js_array();
            if (!arr) return JS_ERR_ALLOC;
            for (int i = 0; i < src->u.array.len; i++) {
                js_value* elem = NULL;
                js_status s = js_clone(&elem, src->u.array.items[i]);
                if (s != JS_OK) { js_free(arr); return s; }
                s = js_push(arr, elem);
                if (s != JS_OK) { js_free(elem); js_free(arr); return s; }
            }
            *out = arr;
            return JS_OK;
        }
        case JS_OBJECT: {
            js_value* obj = js_object();
            if (!obj) return JS_ERR_ALLOC;
            for (int i = 0; i < src->u.object.len; i++) {
                js_value* val = NULL;
                js_status s = js_clone(&val, src->u.object.pairs[i].val);
                if (s != JS_OK) { js_free(obj); return s; }
                s = js_set(obj, src->u.object.pairs[i].key, val);
                if (s != JS_OK) { js_free(val); js_free(obj); return s; }
            }
            *out = obj;
            return JS_OK;
        }
    }
    return JS_ERR_TYPE;
}

// ── Creation Implementation ─────────────────────────────────────────────────

JSDEF js_value* js_null(void)
{
    return js__alloc_value(JS_NULL);
}

JSDEF js_value* js_bool(bool val)
{
    js_value* v = js__alloc_value(JS_BOOL);
    if (v) v->u.boolean = val;
    return v;
}

JSDEF js_value* js_int(int64_t val)
{
    js_value* v = js__alloc_value(JS_INT);
    if (v) v->u.integer = val;
    return v;
}

JSDEF js_value* js_float(double val)
{
    js_value* v = js__alloc_value(JS_FLOAT);
    if (v) v->u.floating = val;
    return v;
}

JSDEF js_value* js_string(const char* val)
{
    if (!val) return NULL;
    js_value* v = js__alloc_value(JS_STRING);
    if (!v) return NULL;
    v->u.string = js__strdup(val);
    if (!v->u.string) { JS_FREE(v); return NULL; }
    return v;
}

JSDEF js_value* js_array(void)
{
    js_value* v = js__alloc_value(JS_ARRAY);
    if (!v) return NULL;
    v->u.array.items = NULL;
    v->u.array.len = 0;
    v->u.array.cap = 0;
    return v;
}

JSDEF js_value* js_object(void)
{
    js_value* v = js__alloc_value(JS_OBJECT);
    if (!v) return NULL;
    v->u.object.pairs = NULL;
    v->u.object.len = 0;
    v->u.object.cap = 0;
    return v;
}

// ── Type Queries Implementation ─────────────────────────────────────────────

JSDEF js_type js_typeof(const js_value* v)
{
    return v ? v->type : JS_NULL;
}

JSDEF bool js_is_null(const js_value* v)   { return !v || v->type == JS_NULL; }
JSDEF bool js_is_bool(const js_value* v)   { return v && v->type == JS_BOOL; }
JSDEF bool js_is_int(const js_value* v)    { return v && v->type == JS_INT; }
JSDEF bool js_is_float(const js_value* v)  { return v && v->type == JS_FLOAT; }
JSDEF bool js_is_string(const js_value* v) { return v && v->type == JS_STRING; }
JSDEF bool js_is_array(const js_value* v)  { return v && v->type == JS_ARRAY; }
JSDEF bool js_is_object(const js_value* v) { return v && v->type == JS_OBJECT; }

// ── Accessors Implementation ────────────────────────────────────────────────

JSDEF bool js_get_bool(const js_value* v)
{
    return (v && v->type == JS_BOOL) ? v->u.boolean : false;
}

JSDEF int64_t js_get_int(const js_value* v)
{
    return (v && v->type == JS_INT) ? v->u.integer : 0;
}

JSDEF double js_get_float(const js_value* v)
{
    if (!v) return 0.0;
    if (v->type == JS_FLOAT) return v->u.floating;
    if (v->type == JS_INT) return (double)v->u.integer;
    return 0.0;
}

JSDEF const char* js_get_string(const js_value* v)
{
    return (v && v->type == JS_STRING) ? v->u.string : NULL;
}

JSDEF js_value* js_get(const js_value* obj, const char* key)
{
    if (!obj || obj->type != JS_OBJECT || !key) return NULL;
    for (int i = 0; i < obj->u.object.len; i++) {
        if (strcmp(obj->u.object.pairs[i].key, key) == 0)
            return obj->u.object.pairs[i].val;
    }
    return NULL;
}

JSDEF js_value* js_at(const js_value* arr, int index)
{
    if (!arr || arr->type != JS_ARRAY) return NULL;
    if (index < 0 || index >= arr->u.array.len) return NULL;
    return arr->u.array.items[index];
}

// ── Mutation Implementation ─────────────────────────────────────────────────

JSDEF js_status js_set(js_value* obj, const char* key, js_value* val)
{
    if (!obj || !key || !val) return JS_ERR_NULL_ARG;
    if (obj->type != JS_OBJECT) return JS_ERR_TYPE;

    // Check if key already exists — replace value
    for (int i = 0; i < obj->u.object.len; i++) {
        if (strcmp(obj->u.object.pairs[i].key, key) == 0) {
            js_free(obj->u.object.pairs[i].val);
            obj->u.object.pairs[i].val = val;
            return JS_OK;
        }
    }

    // Grow if needed
    if (obj->u.object.len >= obj->u.object.cap) {
        int newcap = obj->u.object.cap == 0 ? 4 : obj->u.object.cap * 2;
        js_pair* np = (js_pair*)JS_REALLOC(obj->u.object.pairs,
                                           (size_t)newcap * sizeof(js_pair));
        if (!np) return JS_ERR_ALLOC;
        obj->u.object.pairs = np;
        obj->u.object.cap = newcap;
    }

    char* kdup = js__strdup(key);
    if (!kdup) return JS_ERR_ALLOC;
    obj->u.object.pairs[obj->u.object.len].key = kdup;
    obj->u.object.pairs[obj->u.object.len].val = val;
    obj->u.object.len++;
    return JS_OK;
}

JSDEF js_status js_push(js_value* arr, js_value* val)
{
    if (!arr || !val) return JS_ERR_NULL_ARG;
    if (arr->type != JS_ARRAY) return JS_ERR_TYPE;

    if (arr->u.array.len >= arr->u.array.cap) {
        int newcap = arr->u.array.cap == 0 ? 4 : arr->u.array.cap * 2;
        js_value** ni = (js_value**)JS_REALLOC(arr->u.array.items,
                                               (size_t)newcap * sizeof(js_value*));
        if (!ni) return JS_ERR_ALLOC;
        arr->u.array.items = ni;
        arr->u.array.cap = newcap;
    }

    arr->u.array.items[arr->u.array.len++] = val;
    return JS_OK;
}

JSDEF int js_array_len(const js_value* v)
{
    return (v && v->type == JS_ARRAY) ? v->u.array.len : 0;
}

JSDEF int js_object_len(const js_value* v)
{
    return (v && v->type == JS_OBJECT) ? v->u.object.len : 0;
}

// ── Serialization Implementation ────────────────────────────────────────────

JSDEF js_status js_dump(const js_value* v, char** out, int indent)
{
    if (!out) return JS_ERR_NULL_ARG;
    js__strbuf sb;
    js_status s = js__sb_init(&sb);
    if (s != JS_OK) return s;
    s = js__dump_value(&sb, v, indent, 0);
    if (s != JS_OK) { JS_FREE(sb.buf); return s; }
    *out = sb.buf;
    return JS_OK;
}

JSDEF js_status js_dump_file(const js_value* v, const char* path, int indent)
{
    if (!path) return JS_ERR_NULL_ARG;
    char* str = NULL;
    js_status s = js_dump(v, &str, indent);
    if (s != JS_OK) return s;

    FILE* f = fopen(path, "wb");
    if (!f) { JS_FREE(str); return JS_ERR_IO; }
    size_t len = strlen(str);
    size_t wr = fwrite(str, 1, len, f);
    fwrite("\n", 1, 1, f);
    fclose(f);
    JS_FREE(str);
    return (wr == len) ? JS_OK : JS_ERR_IO;
}

// ── Iteration Implementation ────────────────────────────────────────────────

JSDEF js_object_iter js_object_iter_init(const js_value* obj)
{
    return (js_object_iter){ .obj = obj, .idx = -1 };
}

JSDEF bool js_object_next(js_object_iter* it)
{
    if (!it || !it->obj || it->obj->type != JS_OBJECT) return false;
    it->idx++;
    return it->idx < it->obj->u.object.len;
}

JSDEF const char* js_object_key(const js_object_iter* it)
{
    if (!it || !it->obj || it->obj->type != JS_OBJECT) return NULL;
    if (it->idx < 0 || it->idx >= it->obj->u.object.len) return NULL;
    return it->obj->u.object.pairs[it->idx].key;
}

JSDEF js_value* js_object_val(const js_object_iter* it)
{
    if (!it || !it->obj || it->obj->type != JS_OBJECT) return NULL;
    if (it->idx < 0 || it->idx >= it->obj->u.object.len) return NULL;
    return it->obj->u.object.pairs[it->idx].val;
}

// ── Utilities Implementation ────────────────────────────────────────────────

JSDEF const char* js_status_str(js_status s)
{
    switch (s) {
        case JS_OK:            return "JS_OK";
        case JS_ERR_NULL_ARG:  return "JS_ERR_NULL_ARG";
        case JS_ERR_ALLOC:     return "JS_ERR_ALLOC";
        case JS_ERR_PARSE:     return "JS_ERR_PARSE";
        case JS_ERR_TYPE:      return "JS_ERR_TYPE";
        case JS_ERR_NOT_FOUND: return "JS_ERR_NOT_FOUND";
        case JS_ERR_IO:        return "JS_ERR_IO";
    }
    return "JS_UNKNOWN";
}

JSDEF const char* js_version_str(void)
{
    static char buf[32];
    snprintf(buf, sizeof(buf), "%d.%d.%d",
             JS_VERSION_MAJOR, JS_VERSION_MINOR, JS_VERSION_PATCH);
    return buf;
}

JSDEF bool js_contains(const js_value* obj, const char* key)
{
    return js_get(obj, key) != NULL;
}

#endif // JS_IMPLEMENTATION
#endif // JSON_H
