/*
 * make.c - Minimal make utility for the free toolchain
 * Usage: free-make [-f makefile] [-C dir] [-j N] [target ...]
 * Pure C89, uses system libc for fork/exec/stat.
 */

/* POSIX declarations for popen, snprintf, setenv, realpath */
#define _XOPEN_SOURCE 600

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>

/* ---- limits ---- */
#define MAX_RULES       1024
#define MAX_VARS        512
#define MAX_PREREQS     256
#define MAX_CMDS        64
#define MAX_INCLUDES    32
#define MAX_JOBS        64
#define LINE_MAX_LEN    4096
#define VAR_VAL_MAX     8192
#define EXPAND_MAX      16384
#define PATH_MAX_LEN    4096
#define MAX_PATTERN_RULES 128

/* ---- data structures ---- */

typedef struct {
    char *name;
    char *value;
    int origin; /* 0=file, 1=env, 2=cmdline */
} Var;

typedef struct {
    char *target;
    char *prereqs[MAX_PREREQS];
    int nprereqs;
    char *cmds[MAX_CMDS];
    int ncmds;
    int is_phony;
    int is_pattern;       /* contains % */
    char *pattern_target; /* e.g. "%.o" */
    char *pattern_prereq; /* e.g. "%.c" */
    int visited;
    int building;
} Rule;

typedef struct {
    Rule rules[MAX_RULES];
    int nrules;
    Rule pattern_rules[MAX_PATTERN_RULES];
    int npattern_rules;
    Var vars[MAX_VARS];
    int nvars;
    char *phony_targets[MAX_PREREQS];
    int nphony;
    char *default_target;
    int max_jobs;
    int dry_run;
    int keep_going;
    int silent;
    int question_mode;
} MakeState;

static MakeState state;

/* ---- forward declarations ---- */
static int build_target(const char *target);
static char *expand_vars(const char *input);
static int parse_makefile(const char *filename);

/* ---- string utilities ---- */

static char *my_strdup(const char *s)
{
    size_t len = strlen(s);
    char *p = (char *)malloc(len + 1);
    if (!p) {
        fprintf(stderr, "make: out of memory\n");
        exit(2);
    }
    memcpy(p, s, len + 1);
    return p;
}

static char *skip_spaces(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return (char *)s;
}

static char *rtrim(char *s)
{
    char *end = s + strlen(s) - 1;
    while (end >= s && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r'))
        *end-- = '\0';
    return s;
}

static int starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

/* ---- variable handling ---- */

static Var *find_var(const char *name)
{
    int i;
    for (i = 0; i < state.nvars; i++) {
        if (strcmp(state.vars[i].name, name) == 0)
            return &state.vars[i];
    }
    return NULL;
}

static void set_var(const char *name, const char *value, int origin)
{
    Var *v = find_var(name);
    if (v) {
        if (v->origin <= origin) {
            free(v->value);
            v->value = my_strdup(value);
            v->origin = origin;
        }
        return;
    }
    if (state.nvars >= MAX_VARS) {
        fprintf(stderr, "make: too many variables\n");
        exit(2);
    }
    v = &state.vars[state.nvars++];
    v->name = my_strdup(name);
    v->value = my_strdup(value);
    v->origin = origin;
}

static const char *get_var(const char *name)
{
    Var *v = find_var(name);
    if (v) return v->value;
    return getenv(name);
}

static void append_var(const char *name, const char *value)
{
    Var *v = find_var(name);
    if (v) {
        size_t olen = strlen(v->value);
        size_t nlen = strlen(value);
        char *newval = (char *)malloc(olen + 1 + nlen + 1);
        if (!newval) {
            fprintf(stderr, "make: out of memory\n");
            exit(2);
        }
        memcpy(newval, v->value, olen);
        newval[olen] = ' ';
        memcpy(newval + olen + 1, value, nlen + 1);
        free(v->value);
        v->value = newval;
    } else {
        set_var(name, value, 0);
    }
}

/* ---- function evaluation ---- */

static char *func_shell(const char *arg)
{
    FILE *fp;
    static char buf[VAR_VAL_MAX];
    size_t total = 0;
    size_t n;
    char *p;

    fp = popen(arg, "r");
    if (!fp) return my_strdup("");
    while ((n = fread(buf + total, 1, sizeof(buf) - total - 1, fp)) > 0)
        total += n;
    pclose(fp);
    buf[total] = '\0';
    /* replace newlines with spaces */
    for (p = buf; *p; p++) {
        if (*p == '\n') *p = ' ';
    }
    rtrim(buf);
    return my_strdup(buf);
}

static char *func_wildcard(const char *pattern)
{
    /* simple glob using opendir/readdir for single-directory patterns */
    static char result[VAR_VAL_MAX];
    char dir_path[PATH_MAX_LEN];
    const char *base_pattern;
    const char *slash;
    DIR *d;
    struct dirent *ent;
    size_t pos = 0;
    int first = 1;

    result[0] = '\0';
    slash = strrchr(pattern, '/');
    if (slash) {
        size_t dlen = (size_t)(slash - pattern);
        if (dlen >= sizeof(dir_path)) dlen = sizeof(dir_path) - 1;
        memcpy(dir_path, pattern, dlen);
        dir_path[dlen] = '\0';
        base_pattern = slash + 1;
    } else {
        strcpy(dir_path, ".");
        base_pattern = pattern;
    }

    d = opendir(dir_path);
    if (!d) return my_strdup("");

    while ((ent = readdir(d)) != NULL) {
        const char *name = ent->d_name;
        int match = 0;

        if (name[0] == '.') continue;

        /* simple wildcard matching: *.ext or * */
        if (strcmp(base_pattern, "*") == 0) {
            match = 1;
        } else if (base_pattern[0] == '*' && base_pattern[1] == '.') {
            const char *ext = base_pattern + 1;
            size_t elen = strlen(ext);
            size_t nlen = strlen(name);
            if (nlen >= elen && strcmp(name + nlen - elen, ext) == 0)
                match = 1;
        } else if (strchr(base_pattern, '*') == NULL) {
            if (strcmp(name, base_pattern) == 0)
                match = 1;
        }

        if (match) {
            size_t nlen;
            char fullpath[PATH_MAX_LEN];
            if (slash) {
                snprintf(fullpath, sizeof(fullpath), "%s/%s", dir_path, name);
            } else {
                snprintf(fullpath, sizeof(fullpath), "%s", name);
            }
            nlen = strlen(fullpath);
            if (pos + nlen + 2 < sizeof(result)) {
                if (!first) result[pos++] = ' ';
                memcpy(result + pos, fullpath, nlen);
                pos += nlen;
                first = 0;
            }
        }
    }
    closedir(d);
    result[pos] = '\0';
    return my_strdup(result);
}

/* helper: simple % pattern match */
static int pattern_match(const char *pattern, const char *str, char *stem,
                         size_t stem_size)
{
    const char *pct = strchr(pattern, '%');
    size_t prefix_len, suffix_len, str_len;

    if (!pct) {
        if (strcmp(pattern, str) == 0) {
            if (stem) stem[0] = '\0';
            return 1;
        }
        return 0;
    }
    prefix_len = (size_t)(pct - pattern);
    suffix_len = strlen(pct + 1);
    str_len = strlen(str);

    if (str_len < prefix_len + suffix_len) return 0;
    if (strncmp(str, pattern, prefix_len) != 0) return 0;
    if (strcmp(str + str_len - suffix_len, pct + 1) != 0) return 0;

    if (stem) {
        size_t slen = str_len - prefix_len - suffix_len;
        if (slen >= stem_size) slen = stem_size - 1;
        memcpy(stem, str + prefix_len, slen);
        stem[slen] = '\0';
    }
    return 1;
}

static char *func_patsubst(const char *from, const char *to, const char *text)
{
    static char result[VAR_VAL_MAX];
    char word[PATH_MAX_LEN];
    char stem[PATH_MAX_LEN];
    const char *p = text;
    size_t pos = 0;
    int first = 1;

    result[0] = '\0';
    while (*p) {
        const char *ws;
        size_t wlen;

        p = skip_spaces(p);
        if (!*p) break;
        ws = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        wlen = (size_t)(p - ws);
        if (wlen >= sizeof(word)) wlen = sizeof(word) - 1;
        memcpy(word, ws, wlen);
        word[wlen] = '\0';

        if (!first && pos < sizeof(result) - 1) result[pos++] = ' ';
        first = 0;

        if (pattern_match(from, word, stem, sizeof(stem))) {
            /* substitute */
            const char *pct = strchr(to, '%');
            if (pct) {
                size_t pre = (size_t)(pct - to);
                size_t slen = strlen(stem);
                size_t suf = strlen(pct + 1);
                if (pos + pre + slen + suf < sizeof(result)) {
                    memcpy(result + pos, to, pre);
                    pos += pre;
                    memcpy(result + pos, stem, slen);
                    pos += slen;
                    memcpy(result + pos, pct + 1, suf);
                    pos += suf;
                }
            } else {
                size_t tlen = strlen(to);
                if (pos + tlen < sizeof(result)) {
                    memcpy(result + pos, to, tlen);
                    pos += tlen;
                }
            }
        } else {
            if (pos + wlen < sizeof(result)) {
                memcpy(result + pos, word, wlen);
                pos += wlen;
            }
        }
    }
    result[pos] = '\0';
    return my_strdup(result);
}

static char *func_foreach(const char *var, const char *list, const char *body)
{
    static char result[VAR_VAL_MAX];
    char word[PATH_MAX_LEN];
    const char *p = list;
    size_t pos = 0;
    int first = 1;
    const char *old_val;
    Var *v;

    old_val = get_var(var);
    if (old_val) old_val = my_strdup(old_val);

    result[0] = '\0';
    while (*p) {
        const char *ws;
        size_t wlen;
        char *expanded;

        p = skip_spaces(p);
        if (!*p) break;
        ws = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        wlen = (size_t)(p - ws);
        if (wlen >= sizeof(word)) wlen = sizeof(word) - 1;
        memcpy(word, ws, wlen);
        word[wlen] = '\0';

        set_var(var, word, 0);
        expanded = expand_vars(body);

        if (!first && pos < sizeof(result) - 1) result[pos++] = ' ';
        first = 0;

        wlen = strlen(expanded);
        if (pos + wlen < sizeof(result)) {
            memcpy(result + pos, expanded, wlen);
            pos += wlen;
        }
        free(expanded);
    }
    result[pos] = '\0';

    /* restore old value */
    if (old_val) {
        set_var(var, old_val, 0);
        free((char *)old_val);
    } else {
        v = find_var(var);
        if (v) {
            free(v->name);
            free(v->value);
            /* remove by shifting */
            {
                int idx = (int)(v - state.vars);
                int i;
                for (i = idx; i < state.nvars - 1; i++)
                    state.vars[i] = state.vars[i + 1];
                state.nvars--;
            }
        }
    }
    return my_strdup(result);
}

static char *func_filter(const char *patterns, const char *text)
{
    static char result[VAR_VAL_MAX];
    char word[PATH_MAX_LEN];
    char pat[PATH_MAX_LEN];
    const char *p;
    size_t pos = 0;
    int first = 1;

    result[0] = '\0';
    p = text;
    while (*p) {
        const char *ws;
        size_t wlen;
        const char *pp;
        int matched = 0;

        p = skip_spaces(p);
        if (!*p) break;
        ws = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        wlen = (size_t)(p - ws);
        if (wlen >= sizeof(word)) wlen = sizeof(word) - 1;
        memcpy(word, ws, wlen);
        word[wlen] = '\0';

        pp = patterns;
        while (*pp && !matched) {
            const char *ps;
            size_t plen;
            pp = skip_spaces(pp);
            if (!*pp) break;
            ps = pp;
            while (*pp && *pp != ' ' && *pp != '\t') pp++;
            plen = (size_t)(pp - ps);
            if (plen >= sizeof(pat)) plen = sizeof(pat) - 1;
            memcpy(pat, ps, plen);
            pat[plen] = '\0';
            if (pattern_match(pat, word, NULL, 0))
                matched = 1;
        }
        if (matched) {
            if (!first && pos < sizeof(result) - 1) result[pos++] = ' ';
            first = 0;
            if (pos + wlen < sizeof(result)) {
                memcpy(result + pos, word, wlen);
                pos += wlen;
            }
        }
    }
    result[pos] = '\0';
    return my_strdup(result);
}

static char *func_filter_out(const char *patterns, const char *text)
{
    static char result[VAR_VAL_MAX];
    char word[PATH_MAX_LEN];
    char pat[PATH_MAX_LEN];
    const char *p;
    size_t pos = 0;
    int first = 1;

    result[0] = '\0';
    p = text;
    while (*p) {
        const char *ws;
        size_t wlen;
        const char *pp;
        int matched = 0;

        p = skip_spaces(p);
        if (!*p) break;
        ws = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        wlen = (size_t)(p - ws);
        if (wlen >= sizeof(word)) wlen = sizeof(word) - 1;
        memcpy(word, ws, wlen);
        word[wlen] = '\0';

        pp = patterns;
        while (*pp && !matched) {
            const char *ps;
            size_t plen;
            pp = skip_spaces(pp);
            if (!*pp) break;
            ps = pp;
            while (*pp && *pp != ' ' && *pp != '\t') pp++;
            plen = (size_t)(pp - ps);
            if (plen >= sizeof(pat)) plen = sizeof(pat) - 1;
            memcpy(pat, ps, plen);
            pat[plen] = '\0';
            if (pattern_match(pat, word, NULL, 0))
                matched = 1;
        }
        if (!matched) {
            if (!first && pos < sizeof(result) - 1) result[pos++] = ' ';
            first = 0;
            if (pos + wlen < sizeof(result)) {
                memcpy(result + pos, word, wlen);
                pos += wlen;
            }
        }
    }
    result[pos] = '\0';
    return my_strdup(result);
}

static char *func_sort(const char *text)
{
    static char result[VAR_VAL_MAX];
    char *words[1024];
    int nwords = 0;
    char *copy;
    char *p;
    int i, j;
    size_t pos;

    copy = my_strdup(text);
    p = copy;
    while (*p && nwords < 1024) {
        char *ws;
        p = skip_spaces(p);
        if (!*p) break;
        ws = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = '\0';
        words[nwords++] = ws;
    }

    /* bubble sort + dedup */
    for (i = 0; i < nwords - 1; i++) {
        for (j = 0; j < nwords - 1 - i; j++) {
            if (strcmp(words[j], words[j+1]) > 0) {
                char *tmp = words[j];
                words[j] = words[j+1];
                words[j+1] = tmp;
            }
        }
    }

    pos = 0;
    result[0] = '\0';
    for (i = 0; i < nwords; i++) {
        size_t wlen;
        if (i > 0 && strcmp(words[i], words[i-1]) == 0) continue;
        if (pos > 0 && pos < sizeof(result) - 1) result[pos++] = ' ';
        wlen = strlen(words[i]);
        if (pos + wlen < sizeof(result)) {
            memcpy(result + pos, words[i], wlen);
            pos += wlen;
        }
    }
    result[pos] = '\0';
    free(copy);
    return my_strdup(result);
}

static int count_words(const char *text)
{
    int n = 0;
    const char *p = text;
    while (*p) {
        p = skip_spaces(p);
        if (!*p) break;
        n++;
        while (*p && *p != ' ' && *p != '\t') p++;
    }
    return n;
}

static char *get_word(const char *text, int idx)
{
    static char word[PATH_MAX_LEN];
    int n = 0;
    const char *p = text;

    while (*p) {
        const char *ws;
        size_t wlen;
        p = skip_spaces(p);
        if (!*p) break;
        ws = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (n == idx) {
            wlen = (size_t)(p - ws);
            if (wlen >= sizeof(word)) wlen = sizeof(word) - 1;
            memcpy(word, ws, wlen);
            word[wlen] = '\0';
            return word;
        }
        n++;
    }
    return "";
}

/* path functions */
static char *func_dir(const char *text)
{
    static char result[VAR_VAL_MAX];
    char word[PATH_MAX_LEN];
    const char *p = text;
    size_t pos = 0;
    int first = 1;

    result[0] = '\0';
    while (*p) {
        const char *ws;
        size_t wlen;
        const char *slash;

        p = skip_spaces(p);
        if (!*p) break;
        ws = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        wlen = (size_t)(p - ws);
        if (wlen >= sizeof(word)) wlen = sizeof(word) - 1;
        memcpy(word, ws, wlen);
        word[wlen] = '\0';

        if (!first && pos < sizeof(result) - 1) result[pos++] = ' ';
        first = 0;

        slash = strrchr(word, '/');
        if (slash) {
            size_t dlen = (size_t)(slash - word) + 1;
            if (pos + dlen < sizeof(result)) {
                memcpy(result + pos, word, dlen);
                pos += dlen;
            }
        } else {
            if (pos + 2 < sizeof(result)) {
                result[pos++] = '.';
                result[pos++] = '/';
            }
        }
    }
    result[pos] = '\0';
    return my_strdup(result);
}

static char *func_notdir(const char *text)
{
    static char result[VAR_VAL_MAX];
    char word[PATH_MAX_LEN];
    const char *p = text;
    size_t pos = 0;
    int first = 1;

    result[0] = '\0';
    while (*p) {
        const char *ws;
        size_t wlen;
        const char *slash;
        const char *base;

        p = skip_spaces(p);
        if (!*p) break;
        ws = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        wlen = (size_t)(p - ws);
        if (wlen >= sizeof(word)) wlen = sizeof(word) - 1;
        memcpy(word, ws, wlen);
        word[wlen] = '\0';

        if (!first && pos < sizeof(result) - 1) result[pos++] = ' ';
        first = 0;

        slash = strrchr(word, '/');
        base = slash ? slash + 1 : word;
        wlen = strlen(base);
        if (pos + wlen < sizeof(result)) {
            memcpy(result + pos, base, wlen);
            pos += wlen;
        }
    }
    result[pos] = '\0';
    return my_strdup(result);
}

static char *func_basename(const char *text)
{
    static char result[VAR_VAL_MAX];
    char word[PATH_MAX_LEN];
    const char *p = text;
    size_t pos = 0;
    int first = 1;

    result[0] = '\0';
    while (*p) {
        const char *ws;
        size_t wlen;
        char *dot;

        p = skip_spaces(p);
        if (!*p) break;
        ws = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        wlen = (size_t)(p - ws);
        if (wlen >= sizeof(word)) wlen = sizeof(word) - 1;
        memcpy(word, ws, wlen);
        word[wlen] = '\0';

        if (!first && pos < sizeof(result) - 1) result[pos++] = ' ';
        first = 0;

        dot = strrchr(word, '.');
        if (dot && dot != word) {
            /* don't strip if dot is before last slash */
            const char *slash = strrchr(word, '/');
            if (!slash || dot > slash) {
                *dot = '\0';
            }
        }
        wlen = strlen(word);
        if (pos + wlen < sizeof(result)) {
            memcpy(result + pos, word, wlen);
            pos += wlen;
        }
    }
    result[pos] = '\0';
    return my_strdup(result);
}

static char *func_suffix(const char *text)
{
    static char result[VAR_VAL_MAX];
    char word[PATH_MAX_LEN];
    const char *p = text;
    size_t pos = 0;
    int first = 1;

    result[0] = '\0';
    while (*p) {
        const char *ws;
        size_t wlen;
        const char *dot;

        p = skip_spaces(p);
        if (!*p) break;
        ws = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        wlen = (size_t)(p - ws);
        if (wlen >= sizeof(word)) wlen = sizeof(word) - 1;
        memcpy(word, ws, wlen);
        word[wlen] = '\0';

        dot = strrchr(word, '.');
        if (dot) {
            const char *slash = strrchr(word, '/');
            if (!slash || dot > slash) {
                size_t slen = strlen(dot);
                if (!first && pos < sizeof(result) - 1) result[pos++] = ' ';
                first = 0;
                if (pos + slen < sizeof(result)) {
                    memcpy(result + pos, dot, slen);
                    pos += slen;
                }
            }
        }
    }
    result[pos] = '\0';
    return my_strdup(result);
}

static char *func_addprefix(const char *prefix, const char *text)
{
    static char result[VAR_VAL_MAX];
    const char *p = text;
    size_t pos = 0;
    size_t plen = strlen(prefix);
    int first = 1;

    result[0] = '\0';
    while (*p) {
        const char *ws;
        size_t wlen;

        p = skip_spaces(p);
        if (!*p) break;
        ws = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        wlen = (size_t)(p - ws);

        if (!first && pos < sizeof(result) - 1) result[pos++] = ' ';
        first = 0;

        if (pos + plen + wlen < sizeof(result)) {
            memcpy(result + pos, prefix, plen);
            pos += plen;
            memcpy(result + pos, ws, wlen);
            pos += wlen;
        }
    }
    result[pos] = '\0';
    return my_strdup(result);
}

static char *func_addsuffix(const char *suffix, const char *text)
{
    static char result[VAR_VAL_MAX];
    const char *p = text;
    size_t pos = 0;
    size_t slen = strlen(suffix);
    int first = 1;

    result[0] = '\0';
    while (*p) {
        const char *ws;
        size_t wlen;

        p = skip_spaces(p);
        if (!*p) break;
        ws = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        wlen = (size_t)(p - ws);

        if (!first && pos < sizeof(result) - 1) result[pos++] = ' ';
        first = 0;

        if (pos + wlen + slen < sizeof(result)) {
            memcpy(result + pos, ws, wlen);
            pos += wlen;
            memcpy(result + pos, suffix, slen);
            pos += slen;
        }
    }
    result[pos] = '\0';
    return my_strdup(result);
}

static char *func_if(const char *cond, const char *then_val,
                     const char *else_val)
{
    const char *p = skip_spaces(cond);
    if (*p) return my_strdup(then_val);
    return my_strdup(else_val ? else_val : "");
}

static char *func_strip(const char *text)
{
    static char result[VAR_VAL_MAX];
    const char *p = text;
    size_t pos = 0;
    int first = 1;

    result[0] = '\0';
    while (*p) {
        const char *ws;
        size_t wlen;

        p = skip_spaces(p);
        if (!*p) break;
        ws = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        wlen = (size_t)(p - ws);

        if (!first && pos < sizeof(result) - 1) result[pos++] = ' ';
        first = 0;

        if (pos + wlen < sizeof(result)) {
            memcpy(result + pos, ws, wlen);
            pos += wlen;
        }
    }
    result[pos] = '\0';
    return my_strdup(result);
}

/* ---- find matching paren/brace ---- */
static const char *find_matching(const char *s, char open, char close)
{
    int depth = 1;
    s++;
    while (*s && depth > 0) {
        if (*s == open) depth++;
        else if (*s == close) depth--;
        if (depth > 0) s++;
    }
    return s;
}

/* ---- parse function arguments (comma-separated, respecting parens) ---- */
static int split_func_args(const char *input, size_t len, char args[][VAR_VAL_MAX],
                           int max_args)
{
    int nargs = 0;
    size_t pos = 0;
    size_t apos = 0;
    int depth = 0;

    while (pos < len && nargs < max_args) {
        char c = input[pos];
        if (c == '(' || c == '{') {
            depth++;
            if (apos < VAR_VAL_MAX - 1) args[nargs][apos++] = c;
        } else if (c == ')' || c == '}') {
            depth--;
            if (apos < VAR_VAL_MAX - 1) args[nargs][apos++] = c;
        } else if (c == ',' && depth == 0) {
            args[nargs][apos] = '\0';
            nargs++;
            apos = 0;
        } else {
            if (apos < VAR_VAL_MAX - 1) args[nargs][apos++] = c;
        }
        pos++;
    }
    if (apos > 0 || nargs > 0) {
        args[nargs][apos] = '\0';
        nargs++;
    }
    return nargs;
}

/* ---- evaluate a function call ---- */
static char *eval_function(const char *name, size_t name_len,
                           const char *argstr, size_t argstr_len)
{
    char args[8][VAR_VAL_MAX];
    int nargs;
    char fname[64];
    char *expanded_args[8];
    char *result = NULL;
    int i;

    if (name_len >= sizeof(fname)) name_len = sizeof(fname) - 1;
    memcpy(fname, name, name_len);
    fname[name_len] = '\0';

    nargs = split_func_args(argstr, argstr_len, args, 8);

    /* expand arguments */
    for (i = 0; i < nargs; i++)
        expanded_args[i] = expand_vars(args[i]);
    for (i = nargs; i < 8; i++)
        expanded_args[i] = my_strdup("");

    if (strcmp(fname, "shell") == 0) {
        result = func_shell(expanded_args[0]);
    } else if (strcmp(fname, "wildcard") == 0) {
        result = func_wildcard(expanded_args[0]);
    } else if (strcmp(fname, "patsubst") == 0) {
        result = func_patsubst(expanded_args[0], expanded_args[1],
                               expanded_args[2]);
    } else if (strcmp(fname, "subst") == 0) {
        /* $(subst from,to,text) */
        static char buf[VAR_VAL_MAX];
        const char *from = expanded_args[0];
        const char *to = expanded_args[1];
        const char *text = expanded_args[2];
        size_t flen = strlen(from);
        size_t tlen = strlen(to);
        size_t pos = 0;
        const char *p = text;

        buf[0] = '\0';
        if (flen == 0) {
            result = my_strdup(text);
        } else {
            while (*p) {
                if (strncmp(p, from, flen) == 0) {
                    if (pos + tlen < sizeof(buf)) {
                        memcpy(buf + pos, to, tlen);
                        pos += tlen;
                    }
                    p += flen;
                } else {
                    if (pos < sizeof(buf) - 1) buf[pos++] = *p;
                    p++;
                }
            }
            buf[pos] = '\0';
            result = my_strdup(buf);
        }
    } else if (strcmp(fname, "foreach") == 0) {
        /* don't expand arg[2] yet - foreach does it per iteration */
        free(expanded_args[2]);
        expanded_args[2] = NULL;
        result = func_foreach(expanded_args[0], expanded_args[1], args[2]);
    } else if (strcmp(fname, "if") == 0) {
        result = func_if(expanded_args[0], expanded_args[1],
                        nargs > 2 ? expanded_args[2] : NULL);
    } else if (strcmp(fname, "filter") == 0) {
        result = func_filter(expanded_args[0], expanded_args[1]);
    } else if (strcmp(fname, "filter-out") == 0) {
        result = func_filter_out(expanded_args[0], expanded_args[1]);
    } else if (strcmp(fname, "sort") == 0) {
        result = func_sort(expanded_args[0]);
    } else if (strcmp(fname, "word") == 0) {
        int n = atoi(expanded_args[0]);
        if (n > 0)
            result = my_strdup(get_word(expanded_args[1], n - 1));
        else
            result = my_strdup("");
    } else if (strcmp(fname, "words") == 0) {
        char buf[32];
        sprintf(buf, "%d", count_words(expanded_args[0]));
        result = my_strdup(buf);
    } else if (strcmp(fname, "firstword") == 0) {
        result = my_strdup(get_word(expanded_args[0], 0));
    } else if (strcmp(fname, "lastword") == 0) {
        int n = count_words(expanded_args[0]);
        if (n > 0)
            result = my_strdup(get_word(expanded_args[0], n - 1));
        else
            result = my_strdup("");
    } else if (strcmp(fname, "dir") == 0) {
        result = func_dir(expanded_args[0]);
    } else if (strcmp(fname, "notdir") == 0) {
        result = func_notdir(expanded_args[0]);
    } else if (strcmp(fname, "basename") == 0) {
        result = func_basename(expanded_args[0]);
    } else if (strcmp(fname, "suffix") == 0) {
        result = func_suffix(expanded_args[0]);
    } else if (strcmp(fname, "addprefix") == 0) {
        result = func_addprefix(expanded_args[0], expanded_args[1]);
    } else if (strcmp(fname, "addsuffix") == 0) {
        result = func_addsuffix(expanded_args[0], expanded_args[1]);
    } else if (strcmp(fname, "strip") == 0) {
        result = func_strip(expanded_args[0]);
    } else if (strcmp(fname, "findstring") == 0) {
        if (strstr(expanded_args[1], expanded_args[0]))
            result = my_strdup(expanded_args[0]);
        else
            result = my_strdup("");
    } else if (strcmp(fname, "abspath") == 0 || strcmp(fname, "realpath") == 0) {
        char buf[PATH_MAX_LEN];
        if (realpath(expanded_args[0], buf))
            result = my_strdup(buf);
        else
            result = my_strdup(expanded_args[0]);
    } else if (strcmp(fname, "error") == 0) {
        fprintf(stderr, "*** %s. Stop.\n", expanded_args[0]);
        exit(2);
    } else if (strcmp(fname, "warning") == 0) {
        fprintf(stderr, "make: warning: %s\n", expanded_args[0]);
        result = my_strdup("");
    } else if (strcmp(fname, "info") == 0) {
        printf("%s\n", expanded_args[0]);
        result = my_strdup("");
    } else {
        /* unknown function — treat as variable ref */
        result = my_strdup("");
    }

    for (i = 0; i < 8; i++)
        free(expanded_args[i]);

    return result ? result : my_strdup("");
}

/* ---- known function names ---- */
static const char *func_names[] = {
    "shell", "wildcard", "patsubst", "subst", "foreach", "if",
    "filter-out", "filter", "sort", "word", "words", "firstword",
    "lastword", "dir", "notdir", "basename", "suffix", "addprefix",
    "addsuffix", "strip", "findstring", "abspath", "realpath",
    "error", "warning", "info", NULL
};

static int is_function(const char *name, size_t len)
{
    int i;
    for (i = 0; func_names[i]; i++) {
        if (strlen(func_names[i]) == len &&
            strncmp(name, func_names[i], len) == 0)
            return 1;
    }
    return 0;
}

/* ---- variable expansion ---- */
static char *expand_vars(const char *input)
{
    static int depth = 0;
    char *result;
    size_t rpos = 0;
    size_t rsize = EXPAND_MAX;
    const char *p = input;
    const char *val;

    if (depth > 32) return my_strdup(input);
    depth++;

    result = (char *)malloc(rsize);
    if (!result) {
        fprintf(stderr, "make: out of memory\n");
        exit(2);
    }

    while (*p) {
        if (*p == '$') {
            p++;
            if (*p == '$') {
                /* escaped $ */
                if (rpos < rsize - 1) result[rpos++] = '$';
                p++;
            } else if (*p == '(' || *p == '{') {
                char close = (*p == '(') ? ')' : '}';
                const char *start = p + 1;
                const char *end;
                size_t inner_len;
                char *name_buf;

                end = find_matching(p, *p, close);
                inner_len = (size_t)(end - start);
                name_buf = (char *)malloc(inner_len + 1);
                memcpy(name_buf, start, inner_len);
                name_buf[inner_len] = '\0';

                /* check if it's a function call: $(func args) */
                {
                    const char *sp = name_buf;
                    while (*sp && *sp != ' ' && *sp != '\t') sp++;
                    if (*sp && is_function(name_buf, (size_t)(sp - name_buf))) {
                        size_t fname_len = (size_t)(sp - name_buf);
                        const char *argstart = skip_spaces(sp);
                        size_t arglen = strlen(argstart);
                        char *fres = eval_function(name_buf, fname_len,
                                                   argstart, arglen);
                        size_t flen = strlen(fres);
                        while (rpos + flen >= rsize) {
                            rsize *= 2;
                            result = (char *)realloc(result, rsize);
                        }
                        memcpy(result + rpos, fres, flen);
                        rpos += flen;
                        free(fres);
                        free(name_buf);
                        p = end + 1;
                        continue;
                    }
                }

                /* variable reference */
                {
                    char *exp_name = expand_vars(name_buf);
                    val = get_var(exp_name);
                    if (val) {
                        char *exp_val = expand_vars(val);
                        size_t vlen = strlen(exp_val);
                        while (rpos + vlen >= rsize) {
                            rsize *= 2;
                            result = (char *)realloc(result, rsize);
                        }
                        memcpy(result + rpos, exp_val, vlen);
                        rpos += vlen;
                        free(exp_val);
                    }
                    free(exp_name);
                }

                free(name_buf);
                p = end + 1;
            } else if (*p == '@' || *p == '<' || *p == '^' || *p == '*'
                       || *p == '?') {
                /* automatic variables — single char */
                char vname[2];
                vname[0] = *p;
                vname[1] = '\0';
                val = get_var(vname);
                if (val) {
                    size_t vlen = strlen(val);
                    while (rpos + vlen >= rsize) {
                        rsize *= 2;
                        result = (char *)realloc(result, rsize);
                    }
                    memcpy(result + rpos, val, vlen);
                    rpos += vlen;
                }
                p++;
            } else if (isalnum((unsigned char)*p) || *p == '_') {
                /* single char variable like $X */
                char vname[2];
                vname[0] = *p;
                vname[1] = '\0';
                val = get_var(vname);
                if (val) {
                    size_t vlen = strlen(val);
                    while (rpos + vlen >= rsize) {
                        rsize *= 2;
                        result = (char *)realloc(result, rsize);
                    }
                    memcpy(result + rpos, val, vlen);
                    rpos += vlen;
                }
                p++;
            }
        } else {
            if (rpos < rsize - 1) result[rpos++] = *p;
            p++;
        }
    }
    result[rpos] = '\0';
    depth--;
    return result;
}

/* ---- rule management ---- */

static Rule *find_rule(const char *target)
{
    int i;
    for (i = 0; i < state.nrules; i++) {
        if (strcmp(state.rules[i].target, target) == 0)
            return &state.rules[i];
    }
    return NULL;
}

static int is_phony(const char *target)
{
    int i;
    for (i = 0; i < state.nphony; i++) {
        if (strcmp(state.phony_targets[i], target) == 0)
            return 1;
    }
    return 0;
}

static Rule *find_pattern_rule(const char *target, char *stem, size_t stem_size)
{
    int i;
    for (i = 0; i < state.npattern_rules; i++) {
        if (pattern_match(state.pattern_rules[i].target, target,
                          stem, stem_size))
            return &state.pattern_rules[i];
    }
    return NULL;
}

static Rule *add_rule(const char *target)
{
    Rule *r;
    if (state.nrules >= MAX_RULES) {
        fprintf(stderr, "make: too many rules\n");
        exit(2);
    }
    r = &state.rules[state.nrules++];
    memset(r, 0, sizeof(Rule));
    r->target = my_strdup(target);
    return r;
}

static Rule *add_pattern_rule(const char *target)
{
    Rule *r;
    if (state.npattern_rules >= MAX_PATTERN_RULES) {
        fprintf(stderr, "make: too many pattern rules\n");
        exit(2);
    }
    r = &state.pattern_rules[state.npattern_rules++];
    memset(r, 0, sizeof(Rule));
    r->target = my_strdup(target);
    r->is_pattern = 1;
    return r;
}

/* ---- file timestamps ---- */

static time_t file_mtime(const char *path)
{
    struct stat st;
    if (stat(path, &st) < 0) return 0;
    return st.st_mtime;
}

static int file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

/* ---- command execution ---- */

static int run_command(const char *cmd)
{
    pid_t pid;
    int status;
    int silent_cmd = 0;
    int ignore_err = 0;

    while (*cmd == '@' || *cmd == '-' || *cmd == '+') {
        if (*cmd == '@') silent_cmd = 1;
        else if (*cmd == '-') ignore_err = 1;
        cmd++;
        while (*cmd == ' ' || *cmd == '\t') cmd++;
    }

    if (!silent_cmd && !state.silent) {
        printf("%s\n", cmd);
        fflush(stdout);
    }

    if (state.dry_run) return 0;

    pid = fork();
    if (pid < 0) {
        perror("make: fork");
        return -1;
    }
    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    waitpid(pid, &status, 0);

    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        if (!ignore_err) {
            fprintf(stderr, "make: *** [%s] Error %d\n", cmd,
                    WEXITSTATUS(status));
            return -1;
        }
    }
    if (WIFSIGNALED(status)) {
        fprintf(stderr, "make: *** [%s] Signal %d\n", cmd, WTERMSIG(status));
        if (!ignore_err) return -1;
    }
    return 0;
}

/* ---- building targets ---- */

static int build_rule(Rule *rule, const char *target)
{
    int i;
    time_t target_time;
    int needs_rebuild = 0;

    /* set automatic variables */
    set_var("@", target, 0);
    if (rule->nprereqs > 0) {
        char all_prereqs[VAR_VAL_MAX];
        size_t pos = 0;

        set_var("<", rule->prereqs[0], 0);

        all_prereqs[0] = '\0';
        for (i = 0; i < rule->nprereqs; i++) {
            size_t plen = strlen(rule->prereqs[i]);
            if (i > 0 && pos < sizeof(all_prereqs) - 1)
                all_prereqs[pos++] = ' ';
            if (pos + plen < sizeof(all_prereqs)) {
                memcpy(all_prereqs + pos, rule->prereqs[i], plen);
                pos += plen;
            }
        }
        all_prereqs[pos] = '\0';
        set_var("^", all_prereqs, 0);
    } else {
        set_var("<", "", 0);
        set_var("^", "", 0);
    }

    /* build prerequisites first */
    for (i = 0; i < rule->nprereqs; i++) {
        if (build_target(rule->prereqs[i]) < 0)
            return -1;
    }

    /* check if rebuild needed */
    if (is_phony(target) || rule->is_phony) {
        needs_rebuild = 1;
    } else if (!file_exists(target)) {
        needs_rebuild = 1;
    } else {
        target_time = file_mtime(target);
        for (i = 0; i < rule->nprereqs; i++) {
            if (file_mtime(rule->prereqs[i]) > target_time) {
                needs_rebuild = 1;
                break;
            }
        }
    }

    if (!needs_rebuild) return 0;
    if (state.question_mode) return 1;

    /* update automatic variables again after prereq builds */
    set_var("@", target, 0);
    if (rule->nprereqs > 0) {
        char all_prereqs2[VAR_VAL_MAX];
        size_t pos2 = 0;

        set_var("<", rule->prereqs[0], 0);
        all_prereqs2[0] = '\0';
        for (i = 0; i < rule->nprereqs; i++) {
            size_t plen2 = strlen(rule->prereqs[i]);
            if (i > 0 && pos2 < sizeof(all_prereqs2) - 1)
                all_prereqs2[pos2++] = ' ';
            if (pos2 + plen2 < sizeof(all_prereqs2)) {
                memcpy(all_prereqs2 + pos2, rule->prereqs[i], plen2);
                pos2 += plen2;
            }
        }
        all_prereqs2[pos2] = '\0';
        set_var("^", all_prereqs2, 0);
    }

    /* execute commands */
    for (i = 0; i < rule->ncmds; i++) {
        char *expanded = expand_vars(rule->cmds[i]);
        int rc = run_command(expanded);
        free(expanded);
        if (rc < 0 && !state.keep_going)
            return -1;
    }
    return 0;
}

static int build_target(const char *target)
{
    Rule *rule;
    char stem[PATH_MAX_LEN];
    Rule *prule;

    rule = find_rule(target);
    if (rule) {
        if (rule->building) {
            fprintf(stderr, "make: circular dependency for '%s'\n", target);
            return -1;
        }
        if (rule->visited) return 0;
        rule->building = 1;
        {
            int rc = build_rule(rule, target);
            rule->building = 0;
            rule->visited = 1;
            return rc;
        }
    }

    /* try pattern rules */
    prule = find_pattern_rule(target, stem, sizeof(stem));
    if (prule) {
        /* instantiate pattern rule for this target */
        Rule inst;
        int i;
        int rc;

        memset(&inst, 0, sizeof(inst));
        inst.target = my_strdup(target);
        set_var("*", stem, 0);

        /* expand prereqs */
        for (i = 0; i < prule->nprereqs; i++) {
            char *expanded;
            char prereq_buf[PATH_MAX_LEN];
            const char *pp = prule->prereqs[i];
            const char *pct = strchr(pp, '%');
            if (pct) {
                size_t pre = (size_t)(pct - pp);
                size_t slen = strlen(stem);
                size_t suf = strlen(pct + 1);
                memcpy(prereq_buf, pp, pre);
                memcpy(prereq_buf + pre, stem, slen);
                memcpy(prereq_buf + pre + slen, pct + 1, suf + 1);
                expanded = my_strdup(prereq_buf);
            } else {
                expanded = my_strdup(pp);
            }
            inst.prereqs[inst.nprereqs++] = expanded;
        }
        for (i = 0; i < prule->ncmds; i++)
            inst.cmds[inst.ncmds++] = prule->cmds[i];

        rc = build_rule(&inst, target);
        free(inst.target);
        for (i = 0; i < inst.nprereqs; i++)
            free(inst.prereqs[i]);
        return rc;
    }

    /* no rule — check if file exists */
    if (file_exists(target)) return 0;

    fprintf(stderr, "make: *** No rule to make target '%s'. Stop.\n", target);
    return -1;
}

/* ---- makefile parsing ---- */

/* conditional state */
#define COND_STACK_MAX 32
static int cond_stack[COND_STACK_MAX];
static int cond_depth = 0;

static int cond_active(void)
{
    int i;
    for (i = 0; i < cond_depth; i++) {
        if (!cond_stack[i]) return 0;
    }
    return 1;
}

static int parse_makefile(const char *filename)
{
    FILE *fp;
    char line[LINE_MAX_LEN];
    char cont_buf[LINE_MAX_LEN * 4];
    Rule *current_rule = NULL;
    int lineno = 0;

    fp = fopen(filename, "r");
    if (!fp) return -1;

    cont_buf[0] = '\0';

    while (fgets(line, sizeof(line), fp)) {
        char *p;
        size_t len;

        lineno++;

        /* handle line continuation */
        len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[--len] = '\0';
        if (len > 0 && line[len - 1] == '\r') line[--len] = '\0';

        if (len > 0 && line[len - 1] == '\\') {
            line[len - 1] = ' ';
            strcat(cont_buf, line);
            continue;
        }

        if (cont_buf[0]) {
            strcat(cont_buf, line);
            strcpy(line, cont_buf);
            cont_buf[0] = '\0';
            len = strlen(line);
        }

        /* tab-indented lines are commands for current rule */
        if (line[0] == '\t') {
            if (current_rule && cond_active()) {
                if (current_rule->ncmds < MAX_CMDS) {
                    current_rule->cmds[current_rule->ncmds++] =
                        my_strdup(line + 1);
                }
            }
            continue;
        }

        p = skip_spaces(line);

        /* blank lines / comments */
        if (*p == '\0' || *p == '#') {
            if (*p == '\0') current_rule = NULL;
            continue;
        }

        /* conditionals: ifeq, ifneq, ifdef, ifndef, else, endif */
        if (starts_with(p, "ifeq ") || starts_with(p, "ifeq\t") ||
            starts_with(p, "ifeq(")) {
            const char *arg = p + 4;
            char *left, *right;
            char *exp_l, *exp_r;

            arg = skip_spaces(arg);
            if (*arg == '(') {
                const char *comma;
                const char *end;
                arg++;
                comma = strchr(arg, ',');
                if (!comma) goto skip_cond;
                end = strrchr(comma, ')');
                if (!end) end = comma + strlen(comma);

                left = (char *)malloc((size_t)(comma - arg) + 1);
                memcpy(left, arg, (size_t)(comma - arg));
                left[comma - arg] = '\0';
                rtrim(left);

                comma++;
                comma = skip_spaces(comma);
                right = (char *)malloc((size_t)(end - comma) + 1);
                memcpy(right, comma, (size_t)(end - comma));
                right[end - comma] = '\0';
                rtrim(right);
            } else {
                /* quoted form: ifeq "a" "b" or ifeq 'a' 'b' */
                char delim;
                const char *s1, *e1, *s2, *e2;

                arg = skip_spaces(arg);
                delim = *arg;
                if (delim != '"' && delim != '\'') goto skip_cond;
                s1 = arg + 1;
                e1 = strchr(s1, delim);
                if (!e1) goto skip_cond;
                left = (char *)malloc((size_t)(e1 - s1) + 1);
                memcpy(left, s1, (size_t)(e1 - s1));
                left[e1 - s1] = '\0';

                s2 = skip_spaces(e1 + 1);
                delim = *s2;
                if (delim != '"' && delim != '\'') { free(left); goto skip_cond; }
                s2++;
                e2 = strchr(s2, delim);
                if (!e2) { free(left); goto skip_cond; }
                right = (char *)malloc((size_t)(e2 - s2) + 1);
                memcpy(right, s2, (size_t)(e2 - s2));
                right[e2 - s2] = '\0';
            }

            exp_l = expand_vars(left);
            exp_r = expand_vars(right);

            if (cond_depth < COND_STACK_MAX)
                cond_stack[cond_depth++] = (strcmp(exp_l, exp_r) == 0);

            free(left); free(right);
            free(exp_l); free(exp_r);
            continue;

        skip_cond:
            if (cond_depth < COND_STACK_MAX)
                cond_stack[cond_depth++] = 0;
            continue;
        }

        if (starts_with(p, "ifneq ") || starts_with(p, "ifneq\t") ||
            starts_with(p, "ifneq(")) {
            const char *arg = p + 5;
            char *left, *right;
            char *exp_l, *exp_r;

            arg = skip_spaces(arg);
            if (*arg == '(') {
                const char *comma;
                const char *end;
                arg++;
                comma = strchr(arg, ',');
                if (!comma) { cond_stack[cond_depth++] = 0; continue; }
                end = strrchr(comma, ')');
                if (!end) end = comma + strlen(comma);

                left = (char *)malloc((size_t)(comma - arg) + 1);
                memcpy(left, arg, (size_t)(comma - arg));
                left[comma - arg] = '\0';
                rtrim(left);

                comma++;
                comma = skip_spaces(comma);
                right = (char *)malloc((size_t)(end - comma) + 1);
                memcpy(right, comma, (size_t)(end - comma));
                right[end - comma] = '\0';
                rtrim(right);
            } else {
                cond_stack[cond_depth++] = 0;
                continue;
            }

            exp_l = expand_vars(left);
            exp_r = expand_vars(right);

            if (cond_depth < COND_STACK_MAX)
                cond_stack[cond_depth++] = (strcmp(exp_l, exp_r) != 0);

            free(left); free(right);
            free(exp_l); free(exp_r);
            continue;
        }

        if (starts_with(p, "ifdef ")) {
            const char *vname = skip_spaces(p + 6);
            char vn[256];
            char *exp_name;
            size_t vlen;

            strncpy(vn, vname, sizeof(vn) - 1);
            vn[sizeof(vn) - 1] = '\0';
            rtrim(vn);
            exp_name = expand_vars(vn);
            vlen = strlen(exp_name);
            /* trim trailing space */
            while (vlen > 0 && (exp_name[vlen-1] == ' ' || exp_name[vlen-1] == '\t'))
                exp_name[--vlen] = '\0';

            if (cond_depth < COND_STACK_MAX)
                cond_stack[cond_depth++] = (get_var(exp_name) != NULL);
            free(exp_name);
            continue;
        }

        if (starts_with(p, "ifndef ")) {
            const char *vname = skip_spaces(p + 7);
            char vn[256];
            char *exp_name;
            size_t vlen;

            strncpy(vn, vname, sizeof(vn) - 1);
            vn[sizeof(vn) - 1] = '\0';
            rtrim(vn);
            exp_name = expand_vars(vn);
            vlen = strlen(exp_name);
            while (vlen > 0 && (exp_name[vlen-1] == ' ' || exp_name[vlen-1] == '\t'))
                exp_name[--vlen] = '\0';

            if (cond_depth < COND_STACK_MAX)
                cond_stack[cond_depth++] = (get_var(exp_name) == NULL);
            free(exp_name);
            continue;
        }

        if (strcmp(p, "else") == 0 || starts_with(p, "else ") ||
            starts_with(p, "else\t")) {
            if (cond_depth > 0)
                cond_stack[cond_depth - 1] = !cond_stack[cond_depth - 1];
            continue;
        }

        if (strcmp(p, "endif") == 0) {
            if (cond_depth > 0)
                cond_depth--;
            continue;
        }

        if (!cond_active()) continue;

        /* include directive */
        if (starts_with(p, "include ") || starts_with(p, "-include ") ||
            starts_with(p, "sinclude ")) {
            int optional = (p[0] == '-' || p[0] == 's');
            const char *files;
            char *expanded;
            char *fp2;

            if (p[0] == '-') files = p + 9;
            else if (p[0] == 's') files = p + 9;
            else files = p + 8;

            expanded = expand_vars(skip_spaces(files));
            fp2 = expanded;

            while (*fp2) {
                char fname[PATH_MAX_LEN];
                size_t flen;
                const char *ws;

                fp2 = skip_spaces(fp2);
                if (!*fp2) break;
                ws = fp2;
                while (*fp2 && *fp2 != ' ' && *fp2 != '\t') fp2++;
                flen = (size_t)(fp2 - ws);
                if (flen >= sizeof(fname)) flen = sizeof(fname) - 1;
                memcpy(fname, ws, flen);
                fname[flen] = '\0';

                if (parse_makefile(fname) < 0 && !optional) {
                    fprintf(stderr, "make: %s: No such file or directory\n",
                            fname);
                    free(expanded);
                    fclose(fp);
                    return -1;
                }
            }
            free(expanded);
            continue;
        }

        /* .PHONY declaration */
        if (starts_with(p, ".PHONY")) {
            const char *colon = strchr(p, ':');
            if (colon) {
                const char *pp = skip_spaces(colon + 1);
                char *expanded = expand_vars(pp);
                char *ep = expanded;

                while (*ep) {
                    char target_name[PATH_MAX_LEN];
                    const char *ws;
                    size_t tlen;

                    ep = skip_spaces(ep);
                    if (!*ep) break;
                    ws = ep;
                    while (*ep && *ep != ' ' && *ep != '\t') ep++;
                    tlen = (size_t)(ep - ws);
                    if (tlen >= sizeof(target_name))
                        tlen = sizeof(target_name) - 1;
                    memcpy(target_name, ws, tlen);
                    target_name[tlen] = '\0';

                    if (state.nphony < MAX_PREREQS)
                        state.phony_targets[state.nphony++] =
                            my_strdup(target_name);
                }
                free(expanded);
            }
            continue;
        }

        /* export / unexport */
        if (starts_with(p, "export ")) {
            const char *vp = skip_spaces(p + 7);
            const char *eq = strchr(vp, '=');
            if (eq) {
                char name[256];
                size_t nlen = (size_t)(eq - vp);
                const char *val;
                char *expanded;

                while (nlen > 0 && (vp[nlen-1] == ' ' || vp[nlen-1] == '\t'))
                    nlen--;
                if (nlen >= sizeof(name)) nlen = sizeof(name) - 1;
                memcpy(name, vp, nlen);
                name[nlen] = '\0';

                val = skip_spaces(eq + 1);
                expanded = expand_vars(val);
                set_var(name, expanded, 0);
                setenv(name, expanded, 1);
                free(expanded);
            } else {
                char vname[256];
                const char *val;

                strncpy(vname, vp, sizeof(vname) - 1);
                vname[sizeof(vname) - 1] = '\0';
                rtrim(vname);
                val = get_var(vname);
                if (val) setenv(vname, val, 1);
            }
            continue;
        }

        /* variable assignment: VAR = value, VAR := value, VAR += value,
           VAR ?= value */
        {
            const char *eq = NULL;
            const char *pp = p;
            int assign_type = 0; /* 0=recursive, 1=simple, 2=append, 3=cond */

            /* find = outside of $() */
            while (*pp) {
                if (*pp == '$' && (pp[1] == '(' || pp[1] == '{')) {
                    char close = (pp[1] == '(') ? ')' : '}';
                    pp = find_matching(pp + 1, pp[1], close) + 1;
                    continue;
                }
                if (*pp == ':' && pp[1] == '=') {
                    eq = pp;
                    assign_type = 1;
                    break;
                }
                if (*pp == '+' && pp[1] == '=') {
                    eq = pp;
                    assign_type = 2;
                    break;
                }
                if (*pp == '?' && pp[1] == '=') {
                    eq = pp;
                    assign_type = 3;
                    break;
                }
                if (*pp == '=' && pp != p) {
                    eq = pp;
                    assign_type = 0;
                    break;
                }
                if (*pp == ':') break; /* it's a rule, not assignment */
                pp++;
            }

            if (eq) {
                char name[256];
                const char *val;
                size_t nlen = (size_t)(eq - p);

                while (nlen > 0 && (p[nlen-1] == ' ' || p[nlen-1] == '\t'))
                    nlen--;
                if (nlen >= sizeof(name)) nlen = sizeof(name) - 1;
                memcpy(name, p, nlen);
                name[nlen] = '\0';

                val = (assign_type == 0) ? eq + 1 : eq + 2;
                val = skip_spaces(val);

                if (assign_type == 2) {
                    char *expanded = expand_vars(val);
                    append_var(name, expanded);
                    free(expanded);
                } else if (assign_type == 3) {
                    if (!get_var(name)) {
                        set_var(name, val, 0);
                    }
                } else if (assign_type == 1) {
                    char *expanded = expand_vars(val);
                    set_var(name, expanded, 0);
                    free(expanded);
                } else {
                    set_var(name, val, 0);
                }
                current_rule = NULL;
                continue;
            }
        }

        /* suffix rule: .c.o: */
        if (p[0] == '.' && strlen(p) > 2) {
            const char *colon = strchr(p, ':');
            if (colon) {
                const char *dot2 = strchr(p + 1, '.');
                if (dot2 && dot2 < colon) {
                    /* convert .X.Y: to %.Y: %.X */
                    char src_ext[32], dst_ext[32];
                    char pat_target[64], pat_prereq[64];
                    Rule *r;
                    size_t slen = (size_t)(dot2 - p);
                    size_t dlen = (size_t)(colon - dot2);

                    if (slen >= sizeof(src_ext)) slen = sizeof(src_ext) - 1;
                    memcpy(src_ext, p, slen);
                    src_ext[slen] = '\0';

                    if (dlen >= sizeof(dst_ext)) dlen = sizeof(dst_ext) - 1;
                    memcpy(dst_ext, dot2, dlen);
                    dst_ext[dlen] = '\0';

                    sprintf(pat_target, "%%%s", dst_ext);
                    sprintf(pat_prereq, "%%%s", src_ext);

                    r = add_pattern_rule(pat_target);
                    r->prereqs[r->nprereqs++] = my_strdup(pat_prereq);
                    current_rule = r;
                    continue;
                }
            }
        }

        /* rule: target: prereqs */
        {
            const char *colon = strchr(p, ':');
            if (colon && (colon[1] != '=')) {
                char *expanded_targets;
                char *expanded_prereqs;
                char prereq_str[LINE_MAX_LEN];
                char target_str[LINE_MAX_LEN];
                size_t tlen, plen;
                char *tp;

                tlen = (size_t)(colon - p);
                if (tlen >= sizeof(target_str)) tlen = sizeof(target_str) - 1;
                memcpy(target_str, p, tlen);
                target_str[tlen] = '\0';
                rtrim(target_str);

                plen = strlen(colon + 1);
                if (plen >= sizeof(prereq_str)) plen = sizeof(prereq_str) - 1;
                memcpy(prereq_str, colon + 1, plen);
                prereq_str[plen] = '\0';

                /* handle order-only prereqs (|) — just strip for now */
                {
                    char *pipe = strchr(prereq_str, '|');
                    if (pipe) *pipe = '\0';
                }

                expanded_targets = expand_vars(target_str);
                expanded_prereqs = expand_vars(prereq_str);

                /* handle multiple targets */
                tp = expanded_targets;
                current_rule = NULL;
                while (*tp) {
                    char tname[PATH_MAX_LEN];
                    const char *ws;
                    size_t tnlen;
                    Rule *r;
                    char *pp;

                    tp = skip_spaces(tp);
                    if (!*tp) break;
                    ws = tp;
                    while (*tp && *tp != ' ' && *tp != '\t') tp++;
                    tnlen = (size_t)(tp - ws);
                    if (tnlen >= sizeof(tname)) tnlen = sizeof(tname) - 1;
                    memcpy(tname, ws, tnlen);
                    tname[tnlen] = '\0';

                    /* pattern rule? */
                    if (strchr(tname, '%')) {
                        r = add_pattern_rule(tname);
                    } else {
                        r = find_rule(tname);
                        if (!r) r = add_rule(tname);
                    }

                    /* parse prereqs */
                    pp = expanded_prereqs;
                    while (*pp) {
                        char pname[PATH_MAX_LEN];
                        const char *pws;
                        size_t pnlen;

                        pp = skip_spaces(pp);
                        if (!*pp) break;
                        pws = pp;
                        while (*pp && *pp != ' ' && *pp != '\t') pp++;
                        pnlen = (size_t)(pp - pws);
                        if (pnlen >= sizeof(pname)) pnlen = sizeof(pname) - 1;
                        memcpy(pname, pws, pnlen);
                        pname[pnlen] = '\0';

                        if (r->nprereqs < MAX_PREREQS)
                            r->prereqs[r->nprereqs++] = my_strdup(pname);
                    }

                    if (!state.default_target && !r->is_pattern &&
                        tname[0] != '.') {
                        state.default_target = my_strdup(tname);
                    }

                    current_rule = r;
                }
                free(expanded_targets);
                free(expanded_prereqs);
                continue;
            }
        }
    }

    fclose(fp);
    return 0;
}

/* ---- set default variables ---- */
static void set_defaults(void)
{
    if (!get_var("CC")) set_var("CC", "cc", 0);
    if (!get_var("CXX")) set_var("CXX", "c++", 0);
    if (!get_var("AR")) set_var("AR", "ar", 0);
    if (!get_var("AS")) set_var("AS", "as", 0);
    if (!get_var("LD")) set_var("LD", "ld", 0);
    if (!get_var("CFLAGS")) set_var("CFLAGS", "", 0);
    if (!get_var("CXXFLAGS")) set_var("CXXFLAGS", "", 0);
    if (!get_var("LDFLAGS")) set_var("LDFLAGS", "", 0);
    if (!get_var("CPPFLAGS")) set_var("CPPFLAGS", "", 0);
    if (!get_var("RM")) set_var("RM", "rm -f", 0);

    set_var("MAKE", "free-make", 0);
    set_var("MAKELEVEL", "0", 0);
}

/* ---- add implicit rules ---- */
static void add_implicit_rules(void)
{
    Rule *r;

    /* %.o: %.c */
    r = add_pattern_rule("%.o");
    r->prereqs[r->nprereqs++] = my_strdup("%.c");
    r->cmds[r->ncmds++] = my_strdup("$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<");

    /* %.o: %.S */
    r = add_pattern_rule("%.o");
    r->prereqs[r->nprereqs++] = my_strdup("%.S");
    r->cmds[r->ncmds++] = my_strdup("$(CC) $(CPPFLAGS) $(ASFLAGS) -c -o $@ $<");
}

/* ---- main ---- */
int main(int argc, char **argv)
{
    int i;
    const char *makefile = NULL;
    char *targets[64];
    int ntargets = 0;
    char *cmdline_vars[64];
    int ncmdline_vars = 0;
    int rc = 0;

    memset(&state, 0, sizeof(state));
    state.max_jobs = 1;

    /* parse command line */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            makefile = argv[++i];
        } else if (strcmp(argv[i], "-C") == 0 && i + 1 < argc) {
            if (chdir(argv[++i]) < 0) {
                fprintf(stderr, "make: *** %s: No such file or directory\n",
                        argv[i]);
                return 2;
            }
        } else if (strcmp(argv[i], "-j") == 0 && i + 1 < argc) {
            state.max_jobs = atoi(argv[++i]);
            if (state.max_jobs < 1) state.max_jobs = 1;
            if (state.max_jobs > MAX_JOBS) state.max_jobs = MAX_JOBS;
        } else if (strncmp(argv[i], "-j", 2) == 0 && argv[i][2]) {
            state.max_jobs = atoi(argv[i] + 2);
            if (state.max_jobs < 1) state.max_jobs = 1;
            if (state.max_jobs > MAX_JOBS) state.max_jobs = MAX_JOBS;
        } else if (strcmp(argv[i], "-n") == 0) {
            state.dry_run = 1;
        } else if (strcmp(argv[i], "-k") == 0) {
            state.keep_going = 1;
        } else if (strcmp(argv[i], "-s") == 0 ||
                   strcmp(argv[i], "--silent") == 0 ||
                   strcmp(argv[i], "--quiet") == 0) {
            state.silent = 1;
        } else if (strcmp(argv[i], "-q") == 0) {
            state.question_mode = 1;
        } else if (strcmp(argv[i], "-B") == 0) {
            /* unconditional rebuild — mark everything as needing rebuild */
            /* handled below */
        } else if (strcmp(argv[i], "--version") == 0) {
            printf("GNU make (free-make) 4.3\n");
            return 0;
        } else if (strcmp(argv[i], "--help") == 0 ||
                   strcmp(argv[i], "-h") == 0) {
            printf("Usage: free-make [-f makefile] [-C dir] [-j N] "
                   "[-n] [-k] [-s] [-q] [target ...] [VAR=val ...]\n");
            return 0;
        } else if (strchr(argv[i], '=')) {
            /* VAR=value on command line */
            cmdline_vars[ncmdline_vars++] = argv[i];
        } else if (argv[i][0] != '-') {
            targets[ntargets++] = argv[i];
        }
    }

    set_defaults();
    add_implicit_rules();

    /* apply command-line variable assignments (highest priority) */
    for (i = 0; i < ncmdline_vars; i++) {
        char name[256];
        const char *eq = strchr(cmdline_vars[i], '=');
        size_t nlen;

        if (!eq) continue;
        nlen = (size_t)(eq - cmdline_vars[i]);
        if (nlen >= sizeof(name)) nlen = sizeof(name) - 1;
        memcpy(name, cmdline_vars[i], nlen);
        name[nlen] = '\0';
        set_var(name, eq + 1, 2);
    }

    /* find and parse makefile */
    if (makefile) {
        if (parse_makefile(makefile) < 0) {
            fprintf(stderr, "make: *** No rule to make target '%s'."
                    " Stop.\n", makefile);
            return 2;
        }
    } else {
        if (parse_makefile("GNUmakefile") < 0 &&
            parse_makefile("makefile") < 0 &&
            parse_makefile("Makefile") < 0) {
            fprintf(stderr, "make: *** No targets specified and no makefile "
                    "found. Stop.\n");
            return 2;
        }
    }

    /* build targets */
    if (ntargets == 0) {
        if (!state.default_target) {
            fprintf(stderr, "make: *** No targets. Stop.\n");
            return 2;
        }
        rc = build_target(state.default_target);
    } else {
        for (i = 0; i < ntargets; i++) {
            /* reset visited flags between top-level targets */
            int j;
            for (j = 0; j < state.nrules; j++)
                state.rules[j].visited = 0;
            rc = build_target(targets[i]);
            if (rc < 0 && !state.keep_going) break;
        }
    }

    return (rc < 0) ? 2 : 0;
}
