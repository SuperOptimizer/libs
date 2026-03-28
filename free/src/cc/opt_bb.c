/*
 * opt_bb.c - Basic block optimizer for the free C compiler.
 * Optimizes assembly via basic block analysis: dead block elimination,
 * block merging, jump threading, cond branch simplification, dead
 * store elimination, constant propagation. Pure C89.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* suppress GCC truncation warnings for fixed-size text buffers */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wstringop-truncation"
#endif

#define MAX_LINES  4096
#define MAX_BLOCKS 1024
#define MAX_PREDS  16
#define MAX_LABEL  128
#define NUM_REGS   31
#define REG_BIT(r) (1UL << (r))

struct bb_line {
    char text[256];
    char mnemonic[16];
    int rd, rn, rm;
    long imm;
    int is_branch, is_call, is_label, is_directive, removed;
};

struct basic_block {
    int start, end, succ[2], nsucc;
    int pred[MAX_PREDS], npred, reachable;
    unsigned long live_in, live_out;
};

static struct bb_line lines[MAX_LINES];
static int nlines;
static struct basic_block blocks[MAX_BLOCKS];
static int nblocks;

static int is_ws(char c) { return c == ' ' || c == '\t' || c == '\r'; }

static const char *skip_ws(const char *s)
{
    while (*s && is_ws(*s)) s++;
    return s;
}

static int sw(const char *s, const char *p)
{
    return strncmp(s, p, strlen(p)) == 0;
}

static int parse_reg(const char *s)
{
    int n;
    s = skip_ws(s);
    if (*s != 'x' && *s != 'w') return -1;
    s++;
    if (*s < '0' || *s > '9') return -1;
    n = 0;
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
    return (n <= 30) ? n : -1;
}

static const char *branch_target(const char *text)
{
    const char *p;
    const char *comma;
    p = skip_ws(text);
    while (*p && !is_ws(*p)) p++;
    p = skip_ws(p);
    if (*p != '.' && *p != '\0') {
        comma = strchr(p, ',');
        if (comma) p = skip_ws(comma + 1);
    }
    return (*p == '.') ? p : NULL;
}

static void label_text(const char *text, char *buf, int sz)
{
    const char *p;
    const char *end;
    int len;
    p = skip_ws(text);
    end = strchr(p, ':');
    if (!end) { buf[0] = '\0'; return; }
    len = (int)(end - p);
    if (len >= sz) len = sz - 1;
    memcpy(buf, p, len);
    buf[len] = '\0';
}

static void parse_line(struct bb_line *ln, const char *text)
{
    const char *p;
    const char *ms;
    int ml;
    const char *a2;
    const char *a3;
    const char *t;

    memset(ln, 0, sizeof(*ln));
    strncpy(ln->text, text, sizeof(ln->text) - 1);
    ln->rd = ln->rn = ln->rm = -1;
    p = skip_ws(text);
    if (!*p || *p == '\n') return;
    if (*p == '/' && *(p + 1) == '*') { ln->is_directive = 1; return; }
    if (*p == '.' && !strchr(p, ':'))  { ln->is_directive = 1; return; }
    if (strchr(p, ':')) {
        const char *c;
        int spc = 0;
        for (c = p; c < strchr(p, ':'); c++)
            if (is_ws(*c)) { spc = 1; break; }
        if (!spc) { ln->is_label = 1; return; }
    }
    ms = p;
    while (*p && !is_ws(*p) && *p != '\n') p++;
    ml = (int)(p - ms);
    if (ml > 15) ml = 15;
    memcpy(ln->mnemonic, ms, ml);
    ln->mnemonic[ml] = '\0';

    if (strcmp(ln->mnemonic, "b") == 0 || sw(ln->mnemonic, "b.") ||
        strcmp(ln->mnemonic, "cbz") == 0 || strcmp(ln->mnemonic, "cbnz") == 0 ||
        strcmp(ln->mnemonic, "tbz") == 0 || strcmp(ln->mnemonic, "tbnz") == 0 ||
        strcmp(ln->mnemonic, "ret") == 0)
        ln->is_branch = 1;
    if (strcmp(ln->mnemonic, "bl") == 0 || strcmp(ln->mnemonic, "blr") == 0)
        ln->is_call = 1;

    p = skip_ws(p);
    ln->rd = parse_reg(p);
    a2 = strchr(p, ',');
    if (a2) {
        a2 = skip_ws(a2 + 1);
        ln->rn = parse_reg(a2);
        t = skip_ws(a2);
        if (*t == '#') ln->imm = strtol(t + 1, NULL, 0);
        a3 = strchr(a2, ',');
        if (a3) {
            a3 = skip_ws(a3 + 1);
            ln->rm = parse_reg(a3);
            t = skip_ws(a3);
            if (*t == '#') ln->imm = strtol(t + 1, NULL, 0);
        }
    }
}

static void split_lines(const char *text, int len)
{
    int i;
    int ls = 0;
    nlines = 0;
    for (i = 0; i <= len; i++) {
        if (i == len || text[i] == '\n') {
            if (i > ls && nlines < MAX_LINES) {
                char buf[256];
                int ll = i - ls;
                if (ll > 255) ll = 255;
                memcpy(buf, text + ls, ll);
                buf[ll] = '\0';
                parse_line(&lines[nlines++], buf);
            }
            ls = i + 1;
        }
    }
}

static int is_ubr(int i) { return strcmp(lines[i].mnemonic, "b") == 0; }

static int is_cbr(int i)
{
    return sw(lines[i].mnemonic, "b.") ||
           strcmp(lines[i].mnemonic, "cbz") == 0 ||
           strcmp(lines[i].mnemonic, "cbnz") == 0;
}

static int find_block(const char *label)
{
    int i;
    int j;
    char lbl[MAX_LABEL];
    for (i = 0; i < nblocks; i++)
        for (j = blocks[i].start; j < blocks[i].end; j++)
            if (lines[j].is_label) {
                label_text(lines[j].text, lbl, sizeof(lbl));
                if (strcmp(lbl, label) == 0) return i;
            }
    return -1;
}

static void init_blk(int idx, int s, int e)
{
    blocks[idx].start = s; blocks[idx].end = e;
    blocks[idx].nsucc = blocks[idx].npred = 0;
    blocks[idx].succ[0] = blocks[idx].succ[1] = -1;
    blocks[idx].reachable = 0;
    blocks[idx].live_in = blocks[idx].live_out = 0;
}

static void build_blocks(void)
{
    int i;
    int bs = 0;
    nblocks = 0;
    for (i = 0; i < nlines; i++) {
        if (lines[i].is_label && i > bs && nblocks < MAX_BLOCKS)
            { init_blk(nblocks++, bs, i); bs = i; }
        if ((lines[i].is_branch || strcmp(lines[i].mnemonic, "ret") == 0)
            && nblocks < MAX_BLOCKS)
            { init_blk(nblocks++, bs, i + 1); bs = i + 1; }
    }
    if (bs < nlines && nblocks < MAX_BLOCKS)
        init_blk(nblocks++, bs, nlines);
}

static void add_pred(int b, int p)
{
    int i;
    if (b < 0 || b >= nblocks) return;
    for (i = 0; i < blocks[b].npred; i++)
        if (blocks[b].pred[i] == p) return;
    if (blocks[b].npred < MAX_PREDS) blocks[b].pred[blocks[b].npred++] = p;
}

static void add_edge(int from, int to)
{
    if (blocks[from].nsucc < 2) blocks[from].succ[blocks[from].nsucc++] = to;
    add_pred(to, from);
}

static void build_cfg(void)
{
    int i;
    int last;
    const char *tgt;
    int tb;
    for (i = 0; i < nblocks; i++) {
        last = blocks[i].end - 1;
        if (last < blocks[i].start || lines[last].removed)
            { if (i + 1 < nblocks) add_edge(i, i + 1); continue; }
        if (strcmp(lines[last].mnemonic, "ret") == 0) continue;
        if (is_ubr(last)) {
            tgt = branch_target(lines[last].text);
            if (tgt) { tb = find_block(tgt); if (tb >= 0) add_edge(i, tb); }
            continue;
        }
        if (is_cbr(last)) {
            if (i + 1 < nblocks) add_edge(i, i + 1);
            tgt = branch_target(lines[last].text);
            if (tgt) { tb = find_block(tgt); if (tb >= 0) add_edge(i, tb); }
            continue;
        }
        if (i + 1 < nblocks) add_edge(i, i + 1);
    }
}

/* ---- optimization passes ---- */

static void mark_reach(int idx)
{
    int i;
    if (idx < 0 || idx >= nblocks || blocks[idx].reachable) return;
    blocks[idx].reachable = 1;
    for (i = 0; i < blocks[idx].nsucc; i++) mark_reach(blocks[idx].succ[i]);
}

static int elim_dead_blocks(void)
{
    int c = 0;
    int i;
    int j;
    mark_reach(0);
    for (i = 0; i < nblocks; i++)
        if (!blocks[i].reachable)
            for (j = blocks[i].start; j < blocks[i].end; j++)
                if (!lines[j].removed) { lines[j].removed = 1; c++; }
    return c;
}

static int do_merge(void)
{
    int c = 0;
    int i;
    int s;
    int last;
    for (i = 0; i < nblocks; i++) {
        if (!blocks[i].reachable || blocks[i].nsucc != 1) continue;
        s = blocks[i].succ[0];
        if (s < 0 || s >= nblocks || !blocks[s].reachable) continue;
        if (blocks[s].npred != 1 || blocks[s].pred[0] != i) continue;
        last = blocks[i].end - 1;
        if (last >= blocks[i].start && is_ubr(last) && !lines[last].removed)
            { lines[last].removed = 1; c++; }
        if (blocks[s].start < blocks[s].end && lines[blocks[s].start].is_label)
            { lines[blocks[s].start].removed = 1; c++; }
    }
    return c;
}

static int do_thread(void)
{
    int c = 0;
    int i;
    int last;
    int depth;
    int cb;
    const char *tgt;
    const char *chain;
    char buf[256];
    for (i = 0; i < nblocks; i++) {
        if (!blocks[i].reachable) continue;
        last = blocks[i].end - 1;
        if (last < blocks[i].start || lines[last].removed) continue;
        if (!lines[last].is_branch || strcmp(lines[last].mnemonic, "ret") == 0) continue;
        tgt = branch_target(lines[last].text);
        if (!tgt) continue;
        cb = find_block(tgt);
        if (cb < 0) continue;
        chain = tgt;
        for (depth = 0; depth < 10 && cb >= 0 && blocks[cb].reachable; depth++) {
            const char *nxt;
            int br = -1;
            int k;
            /* check if block is a trivial jump (label + uncond branch only) */
            for (k = blocks[cb].start; k < blocks[cb].end; k++) {
                if (lines[k].removed || lines[k].is_label || lines[k].is_directive) continue;
                if (is_ubr(k)) { if (br >= 0) { br = -2; break; } br = k; }
                else { br = -2; break; }
            }
            if (br < 0) break;
            nxt = branch_target(lines[br].text);
            if (!nxt) break;
            chain = nxt;
            cb = find_block(nxt);
        }
        if (chain != tgt && depth > 0) {
            sprintf(buf, "\t%s %s", lines[last].mnemonic, chain);
            strncpy(lines[last].text, buf, sizeof(lines[last].text) - 1);
            c++;
        }
    }
    return c;
}

static int do_simplify_cond(void)
{
    int c = 0;
    int i;
    int j;
    for (i = 0; i < nblocks; i++) {
        if (!blocks[i].reachable) continue;
        for (j = blocks[i].start + 1; j < blocks[i].end; j++) {
            const char *a;
            const char *b;
            if (lines[j].removed || lines[j - 1].removed) continue;
            if (strcmp(lines[j].mnemonic, "cmp") != 0 ||
                strcmp(lines[j - 1].mnemonic, "cmp") != 0) continue;
            a = skip_ws(lines[j - 1].text);
            while (*a && !is_ws(*a)) a++;
            a = skip_ws(a);
            b = skip_ws(lines[j].text);
            while (*b && !is_ws(*b)) b++;
            b = skip_ws(b);
            if (strcmp(a, b) == 0) { lines[j].removed = 1; c++; }
        }
    }
    return c;
}

/* ---- liveness ---- */

static int rg(int r) { return r >= 0 && r < NUM_REGS; }

static unsigned long lu(int x)
{
    unsigned long m = 0;
    const char *mn;
    int k;
    if (lines[x].is_label || lines[x].is_directive || lines[x].removed) return 0;
    mn = lines[x].mnemonic;
    if (strcmp(mn, "str") == 0 || strcmp(mn, "strb") == 0 ||
        strcmp(mn, "strh") == 0 || strcmp(mn, "stp") == 0 ||
        strcmp(mn, "cmp") == 0) {
        if (rg(lines[x].rd)) m |= REG_BIT(lines[x].rd);
        if (rg(lines[x].rn)) m |= REG_BIT(lines[x].rn);
        return m;
    }
    if (strcmp(mn, "cbz") == 0 || strcmp(mn, "cbnz") == 0)
        return rg(lines[x].rd) ? REG_BIT(lines[x].rd) : 0;
    if (lines[x].is_call) {
        for (k = 0; k <= 7; k++) m |= REG_BIT(k);
        return m;
    }
    if (strcmp(mn, "ret") == 0) return REG_BIT(0);
    if (rg(lines[x].rn)) m |= REG_BIT(lines[x].rn);
    if (rg(lines[x].rm)) m |= REG_BIT(lines[x].rm);
    return m;
}

static unsigned long ld(int x)
{
    unsigned long m = 0;
    const char *mn;
    int k;
    if (lines[x].is_label || lines[x].is_directive || lines[x].removed) return 0;
    mn = lines[x].mnemonic;
    if (strcmp(mn, "str") == 0 || strcmp(mn, "strb") == 0 ||
        strcmp(mn, "strh") == 0 || strcmp(mn, "stp") == 0 ||
        strcmp(mn, "cmp") == 0) return 0;
    if (lines[x].is_branch && !lines[x].is_call) return 0;
    if (lines[x].is_call) {
        for (k = 0; k <= 18; k++) m |= REG_BIT(k);
        return m;
    }
    if (rg(lines[x].rd)) m |= REG_BIT(lines[x].rd);
    return m;
}

static void compute_liveness(void)
{
    int changed = 1;
    int iter;
    int i;
    int j;
    int s;
    unsigned long ni;
    unsigned long no;
    for (i = 0; i < nblocks; i++) blocks[i].live_in = blocks[i].live_out = 0;
    for (iter = 0; changed && iter < 100; iter++) {
        changed = 0;
        for (i = nblocks - 1; i >= 0; i--) {
            if (!blocks[i].reachable) continue;
            no = 0;
            for (s = 0; s < blocks[i].nsucc; s++) {
                int si = blocks[i].succ[s];
                if (si >= 0 && si < nblocks) no |= blocks[si].live_in;
            }
            ni = no;
            for (j = blocks[i].end - 1; j >= blocks[i].start; j--)
                if (!lines[j].removed) ni = lu(j) | (ni & ~ld(j));
            if (ni != blocks[i].live_in || no != blocks[i].live_out)
                { blocks[i].live_in = ni; blocks[i].live_out = no; changed = 1; }
        }
    }
}

static int do_dse(void)
{
    int c = 0;
    int i;
    int j;
    unsigned long live;
    const char *mn;
    for (i = 0; i < nblocks; i++) {
        if (!blocks[i].reachable) continue;
        live = blocks[i].live_out;
        for (j = blocks[i].end - 1; j >= blocks[i].start; j--) {
            unsigned long d;
            if (lines[j].removed || lines[j].is_label || lines[j].is_directive) continue;
            d = ld(j);
            if (d && !(d & live) && !lines[j].is_call && !lines[j].is_branch) {
                mn = lines[j].mnemonic;
                if (strcmp(mn, "mov") == 0 || strcmp(mn, "movz") == 0 ||
                    strcmp(mn, "movn") == 0 || strcmp(mn, "add") == 0 ||
                    strcmp(mn, "sub") == 0 || strcmp(mn, "mul") == 0 ||
                    strcmp(mn, "cset") == 0)
                    { lines[j].removed = 1; c++; continue; }
            }
            live = lu(j) | (live & ~d);
        }
    }
    return c;
}

static int do_constprop(void)
{
    int c = 0;
    int i;
    int j;
    int k;
    for (i = 0; i < nblocks; i++) {
        if (!blocks[i].reachable) continue;
        for (j = blocks[i].start; j < blocks[i].end - 1; j++) {
            int rd;
            long imm;
            const char *p;
            int next;
            if (lines[j].removed || strcmp(lines[j].mnemonic, "mov") != 0) continue;
            rd = lines[j].rd;
            if (!rg(rd)) continue;
            p = skip_ws(lines[j].text);
            while (*p && !is_ws(*p)) p++;
            p = strchr(skip_ws(p), ',');
            if (!p) continue;
            p = skip_ws(p + 1);
            if (*p != '#') continue;
            imm = strtol(p + 1, NULL, 0);
            if (imm < 0 || imm > 4095) continue;
            next = -1;
            for (k = j + 1; k < blocks[i].end; k++)
                if (!lines[k].removed && !lines[k].is_label && !lines[k].is_directive)
                    { next = k; break; }
            if (next < 0 || lines[next].rm != rd) continue;
            if (strcmp(lines[next].mnemonic, "add") != 0 &&
                strcmp(lines[next].mnemonic, "sub") != 0) continue;
            if (lines[next].rd == rd || lines[next].rn == rd) continue;
            {
                char nl[256];
                int ul = 0;
                int m;
                sprintf(nl, "\t%s x%d, x%d, #%ld",
                        lines[next].mnemonic, lines[next].rd, lines[next].rn, imm);
                strncpy(lines[next].text, nl, sizeof(lines[next].text) - 1);
                lines[next].rm = -1;
                lines[next].imm = imm;
                for (m = next + 1; m < blocks[i].end; m++)
                    if (!lines[m].removed && (lu(m) & REG_BIT(rd))) { ul = 1; break; }
                if (blocks[i].live_out & REG_BIT(rd)) ul = 1;
                if (!ul) lines[j].removed = 1;
            }
            c++;
        }
    }
    return c;
}

static int reassemble(char *buf, int cap)
{
    int pos = 0;
    int i;
    int len;
    for (i = 0; i < nlines; i++) {
        if (lines[i].removed) continue;
        len = (int)strlen(lines[i].text);
        if (pos + len + 1 >= cap) break;
        memcpy(buf + pos, lines[i].text, len);
        pos += len;
        buf[pos++] = '\n';
    }
    if (pos < cap) buf[pos] = '\0';
    return pos;
}

/*
 * opt_basic_blocks - optimize assembly text using basic block analysis.
 * Modifies asm_text in-place. Returns the new length.
 * Passes: dead block elimination, block merging, jump threading,
 * cond branch simplification, dead store elimination, const prop.
 */
int opt_basic_blocks(char *asm_text, int len)
{
    int pass;
    int w;

    if (!asm_text || len <= 0) return len;
    split_lines(asm_text, len);
    if (nlines == 0) return len;

    for (pass = 0; pass < 5; pass++) {
        w = 0;
        build_blocks();
        build_cfg();
        w += elim_dead_blocks();
        w += do_merge();
        w += do_thread();
        w += do_simplify_cond();
        compute_liveness();
        w += do_dse();
        w += do_constprop();
        if (!w) break;
    }
    return reassemble(asm_text, len + 1);
}
