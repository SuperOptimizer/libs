/*
 * ir_serialize.c - Serialize/deserialize IR for LTO (.free_ir sections).
 *
 * Binary format: FRIR header, string table, globals, functions with
 * blocks and instructions. Block/value references stored as integer IDs
 * and reconstructed as pointers during deserialization.
 * Pure C89.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "free.h"
#include "ir.h"
#include "elf.h"

/* ---- write buffer ---- */
struct wbuf { unsigned char *d; int len; int cap; };

static void wb_init(struct wbuf *w)
{ w->cap = 4096; w->d = (unsigned char *)malloc((unsigned long)w->cap); w->len = 0; }

static void wb_grow(struct wbuf *w, int n)
{ while (w->len + n > w->cap) { w->cap *= 2; }
  w->d = (unsigned char *)realloc(w->d, (unsigned long)w->cap); }

static void wb_u8(struct wbuf *w, int v)
{ wb_grow(w, 1); w->d[w->len++] = (unsigned char)v; }

static void wb_u16(struct wbuf *w, int v)
{ wb_grow(w, 2);
  w->d[w->len] = (unsigned char)(v & 0xff);
  w->d[w->len+1] = (unsigned char)((v>>8) & 0xff); w->len += 2; }

static void wb_u32(struct wbuf *w, unsigned int v)
{ wb_grow(w, 4);
  w->d[w->len]=(unsigned char)(v&0xff); w->d[w->len+1]=(unsigned char)((v>>8)&0xff);
  w->d[w->len+2]=(unsigned char)((v>>16)&0xff); w->d[w->len+3]=(unsigned char)((v>>24)&0xff);
  w->len += 4; }

static void wb_i64(struct wbuf *w, long v)
{ unsigned long u; int i; u = (unsigned long)v; wb_grow(w, 8);
  for (i = 0; i < 8; i++) { w->d[w->len+i] = (unsigned char)(u&0xff); u >>= 8; }
  w->len += 8; }

static void wb_mem(struct wbuf *w, const void *p, int n)
{ wb_grow(w, n); memcpy(w->d + w->len, p, (unsigned long)n); w->len += n; }

/* ---- read buffer ---- */
struct rbuf { const unsigned char *d; int len; int pos; };

static int rb_ok(struct rbuf *r, int n) { return r->pos + n <= r->len; }
static int rb_u8(struct rbuf *r)
{ return rb_ok(r,1) ? (int)r->d[r->pos++] : 0; }

static int rb_u16(struct rbuf *r)
{ int v; if (!rb_ok(r,2)) return 0;
  v = (int)r->d[r->pos] | ((int)r->d[r->pos+1]<<8); r->pos += 2; return v; }

static int rb_i16(struct rbuf *r)
{ int v; v = rb_u16(r); return (v & 0x8000) ? v - 0x10000 : v; }

static unsigned int rb_u32(struct rbuf *r)
{ unsigned int v; if (!rb_ok(r,4)) return 0;
  v = (unsigned int)r->d[r->pos] | ((unsigned int)r->d[r->pos+1]<<8) |
      ((unsigned int)r->d[r->pos+2]<<16) | ((unsigned int)r->d[r->pos+3]<<24);
  r->pos += 4; return v; }

static long rb_i64(struct rbuf *r)
{ unsigned long v; int i; v = 0; if (!rb_ok(r,8)) return 0;
  for (i = 7; i >= 0; i--) v = (v<<8) | r->d[r->pos+i];
  r->pos += 8; return (long)v; }

/* ---- string table ---- */
#define ST_MAX 4096
struct strtab { char buf[65536]; int len; int off[ST_MAX]; char *str[ST_MAX]; int n; };

static void st_init(struct strtab *s) { s->len = 1; s->buf[0] = '\0'; s->n = 0; }

static int st_add(struct strtab *s, const char *p)
{ int i; int l;
  if (p == NULL || p[0] == '\0') return 0;
  for (i = 0; i < s->n; i++) if (strcmp(s->str[i], p) == 0) return s->off[i];
  l = (int)strlen(p);
  if (s->len+l+1 > (int)sizeof(s->buf) || s->n >= ST_MAX) return 0;
  memcpy(s->buf+s->len, p, (unsigned long)(l+1));
  s->off[s->n] = s->len; s->str[s->n] = s->buf+s->len; s->n++;
  s->len += l+1; return s->len-l-1; }

/* ---- counting helpers ---- */
static int cnt_blks(struct ir_func *fn)
{ struct ir_block *b; int n; n=0; for(b=fn->blocks;b;b=b->next) n++; return n; }

static int cnt_insts(struct ir_block *bb)
{ struct ir_inst *i; int n; n=0; for(i=bb->first;i;i=i->next) n++; return n; }

/* ---- serialize ---- */
#define MAGIC_0 'F'
#define MAGIC_1 'R'
#define MAGIC_2 'I'
#define MAGIC_3 'R'
#define VERSION 1

int ir_serialize(struct ir_module *mod, unsigned char **buf, int *len)
{
    struct wbuf w;
    struct strtab st;
    struct ir_func *fn;
    struct ir_global *g;
    struct ir_block *bb;
    struct ir_inst *it;
    int nf, ng, pi, ai;

    if (!mod) return -1;
    wb_init(&w); st_init(&st);

    /* count */
    nf = 0; for (fn = mod->funcs; fn; fn = fn->next) nf++;
    ng = 0; for (g = mod->globals; g; g = g->next) ng++;

    /* pre-populate strtab */
    for (fn = mod->funcs; fn; fn = fn->next) {
        st_add(&st, fn->name);
        for (bb = fn->blocks; bb; bb = bb->next)
            for (it = bb->first; it; it = it->next)
                st_add(&st, it->name);
    }
    for (g = mod->globals; g; g = g->next) st_add(&st, g->name);

    /* header */
    wb_u8(&w,MAGIC_0); wb_u8(&w,MAGIC_1); wb_u8(&w,MAGIC_2); wb_u8(&w,MAGIC_3);
    wb_u16(&w, VERSION); wb_u16(&w, nf); wb_u16(&w, ng);
    wb_u32(&w, (unsigned int)st.len); wb_mem(&w, st.buf, st.len);

    /* globals */
    for (g = mod->globals; g; g = g->next) {
        wb_u32(&w, (unsigned int)st_add(&st, g->name));
        wb_u8(&w, (int)g->type); wb_u32(&w, (unsigned int)g->size);
        wb_u32(&w, (unsigned int)g->align); wb_i64(&w, g->init_val);
    }

    /* functions */
    for (fn = mod->funcs; fn; fn = fn->next) {
        wb_u32(&w, (unsigned int)st_add(&st, fn->name));
        wb_u8(&w, (int)fn->ret_type); wb_u16(&w, fn->nparams);
        for (pi = 0; pi < fn->nparams; pi++)
            wb_u8(&w, fn->params && fn->params[pi]
                  ? (int)fn->params[pi]->type : (int)IRT_I64);
        wb_u16(&w, cnt_blks(fn)); wb_u32(&w, (unsigned int)fn->next_val_id);

        for (bb = fn->blocks; bb; bb = bb->next) {
            wb_u16(&w, bb->id); wb_u8(&w, bb->npreds);
            for (pi = 0; pi < bb->npreds; pi++)
                wb_u16(&w, bb->preds[pi] ? bb->preds[pi]->id : 0);
            wb_u16(&w, cnt_insts(bb));

            for (it = bb->first; it; it = it->next) {
                wb_u8(&w, (int)it->op);
                wb_u8(&w, it->result ? 1 : 0);
                wb_u16(&w, it->result ? it->result->id : 0);
                wb_u8(&w, it->result ? (int)it->result->type : (int)IRT_VOID);
                wb_u8(&w, it->nargs);
                for (ai = 0; ai < it->nargs; ai++)
                    wb_u16(&w, it->args[ai] ? it->args[ai]->id : 0);
                wb_i64(&w, it->imm);
                wb_u32(&w, (unsigned int)st_add(&st, it->name));
                wb_u16(&w, (int)(short)(it->target ? it->target->id : -1));
                wb_u16(&w, (int)(short)(it->true_bb ? it->true_bb->id : -1));
                wb_u16(&w, (int)(short)(it->false_bb ? it->false_bb->id : -1));
            }
        }
    }
    *buf = w.d; *len = w.len; return 0;
}

/* ---- deserialize ---- */

static struct ir_block *bb_by_id(struct ir_func *fn, int id)
{ struct ir_block *b; for(b=fn->blocks;b;b=b->next) if(b->id==id) return b; return NULL; }

struct ir_module *ir_deserialize(unsigned char *buf, int len, struct arena *a)
{
    struct rbuf r;
    struct ir_module *mod;
    const char *st;
    unsigned int stl;
    int nf, ng, fi, gi, bi, ii, pi;
    struct ir_func *fn_tail, *fn;
    struct ir_global *g_tail;

    r.d = buf; r.len = len; r.pos = 0;
    if (!rb_ok(&r,10)) return NULL;
    if (rb_u8(&r)!=MAGIC_0||rb_u8(&r)!=MAGIC_1||
        rb_u8(&r)!=MAGIC_2||rb_u8(&r)!=MAGIC_3) return NULL;
    if (rb_u16(&r)!=VERSION) return NULL;
    nf = rb_u16(&r); ng = rb_u16(&r);
    stl = rb_u32(&r);
    if (!rb_ok(&r,(int)stl)) return NULL;
    st = (const char *)(r.d + r.pos); r.pos += (int)stl;

    mod = (struct ir_module *)arena_alloc(a, sizeof(*mod));
    memset(mod, 0, sizeof(*mod)); mod->arena = a;

    /* globals */
    g_tail = NULL;
    for (gi = 0; gi < ng; gi++) {
        struct ir_global *g;
        unsigned int no;
        g = (struct ir_global *)arena_alloc(a, sizeof(*g));
        memset(g, 0, sizeof(*g));
        no = rb_u32(&r);
        if (no > 0 && no < stl)
            g->name = str_dup(a, st+no, (int)strlen(st+no));
        g->type = (enum ir_type_kind)rb_u8(&r);
        g->size = (int)rb_u32(&r); g->align = (int)rb_u32(&r);
        g->init_val = rb_i64(&r); g->next = NULL;
        if (!g_tail) mod->globals = g; else g_tail->next = g;
        g_tail = g;
    }

    /* functions */
    fn_tail = NULL;
    for (fi = 0; fi < nf; fi++) {
        unsigned int no;
        int np, nb;
        struct ir_block *bb_tail;

        fn = (struct ir_func *)arena_alloc(a, sizeof(*fn));
        memset(fn, 0, sizeof(*fn));
        no = rb_u32(&r);
        if (no > 0 && no < stl)
            fn->name = str_dup(a, st+no, (int)strlen(st+no));
        fn->ret_type = (enum ir_type_kind)rb_u8(&r);
        np = rb_u16(&r); fn->nparams = np;
        if (np > 0) {
            fn->params = (struct ir_val **)arena_alloc(
                a, (usize)np * sizeof(struct ir_val *));
            for (pi = 0; pi < np; pi++) {
                struct ir_val *pv;
                pv = (struct ir_val *)arena_alloc(a, sizeof(*pv));
                memset(pv, 0, sizeof(*pv));
                pv->type = (enum ir_type_kind)rb_u8(&r);
                pv->id = pi; fn->params[pi] = pv;
            }
        }
        nb = rb_u16(&r); fn->nblocks = nb;
        fn->next_val_id = (int)rb_u32(&r);

        /* blocks */
        bb_tail = NULL;
        for (bi = 0; bi < nb; bi++) {
            struct ir_block *bb;
            int np2, ni;
            struct ir_inst *it_tail;

            bb = (struct ir_block *)arena_alloc(a, sizeof(*bb));
            memset(bb, 0, sizeof(*bb));
            bb->id = rb_u16(&r);
            np2 = rb_u8(&r); bb->npreds = np2; bb->preds_cap = np2;
            if (np2 > 0) {
                bb->preds = (struct ir_block **)arena_alloc(
                    a, (usize)np2 * sizeof(struct ir_block *));
                for (pi = 0; pi < np2; pi++)
                    bb->preds[pi] = (struct ir_block *)(long)rb_u16(&r);
            }
            ni = rb_u16(&r);
            it_tail = NULL;
            for (ii = 0; ii < ni; ii++) {
                struct ir_inst *it;
                int hr, na;
                it = (struct ir_inst *)arena_alloc(a, sizeof(*it));
                memset(it, 0, sizeof(*it));
                it->op = (enum ir_op)rb_u8(&r);
                hr = rb_u8(&r);
                if (hr) {
                    struct ir_val *rv;
                    rv = (struct ir_val *)arena_alloc(a, sizeof(*rv));
                    memset(rv, 0, sizeof(*rv));
                    rv->id = rb_u16(&r);
                    rv->type = (enum ir_type_kind)rb_u8(&r);
                    rv->def = it; it->result = rv;
                } else { rb_u16(&r); rb_u8(&r); }
                na = rb_u8(&r); it->nargs = na;
                if (na > 0) {
                    int ai;
                    it->args = (struct ir_val **)arena_alloc(
                        a, (usize)na * sizeof(struct ir_val *));
                    for (ai = 0; ai < na; ai++) {
                        struct ir_val *av;
                        av = (struct ir_val *)arena_alloc(a, sizeof(*av));
                        memset(av, 0, sizeof(*av));
                        av->id = rb_u16(&r); av->type = IRT_I64;
                        it->args[ai] = av;
                    }
                }
                it->imm = rb_i64(&r);
                { unsigned int ino; ino = rb_u32(&r);
                  if (ino > 0 && ino < stl)
                    it->name = str_dup(a, st+ino, (int)strlen(st+ino)); }
                /* branch targets stored as IDs+1 (0=none) */
                { int t; t = rb_i16(&r);
                  it->target = (struct ir_block *)(long)(t >= 0 ? t+1 : 0);
                  t = rb_i16(&r);
                  it->true_bb = (struct ir_block *)(long)(t >= 0 ? t+1 : 0);
                  t = rb_i16(&r);
                  it->false_bb = (struct ir_block *)(long)(t >= 0 ? t+1 : 0); }
                it->parent = bb; it->prev = it_tail; it->next = NULL;
                if (it_tail) it_tail->next = it; else bb->first = it;
                it_tail = it;
            }
            bb->last = it_tail; bb->next = NULL;
            if (!bb_tail) { fn->blocks = bb; fn->entry = bb; }
            else bb_tail->next = bb;
            bb_tail = bb;
        }

        /* resolve block references */
        { struct ir_block *b;
          for (b = fn->blocks; b; b = b->next) {
            struct ir_inst *i2;
            for (pi = 0; pi < b->npreds; pi++)
                b->preds[pi] = bb_by_id(fn, (int)(long)b->preds[pi]);
            for (i2 = b->first; i2; i2 = i2->next) {
                int t;
                t = (int)(long)i2->target;
                i2->target = t > 0 ? bb_by_id(fn, t-1) : NULL;
                t = (int)(long)i2->true_bb;
                i2->true_bb = t > 0 ? bb_by_id(fn, t-1) : NULL;
                t = (int)(long)i2->false_bb;
                i2->false_bb = t > 0 ? bb_by_id(fn, t-1) : NULL;
            }
          }
        }
        fn->next = NULL;
        if (!fn_tail) mod->funcs = fn; else fn_tail->next = fn;
        fn_tail = fn;
    }
    return mod;
}

/* ---- ELF .free_ir section write ---- */

int ir_write_to_elf(const char *obj_path, struct ir_module *mod)
{
    FILE *f;
    unsigned char *ir;
    int ir_len;
    unsigned char *fb;
    long fsz;
    unsigned long nr;
    Elf64_Ehdr eh;
    Elf64_Shdr *sh, ir_sh, ss_sh;
    char *oss;
    unsigned int oss_sz, si, nss_sz;
    char *nss;
    unsigned long ir_off, ss_off, sh_off, cur;
    int i;
    static const char sn[] = ".free_ir";

    if (ir_serialize(mod, &ir, &ir_len) != 0) return -1;
    f = fopen(obj_path, "rb");
    if (!f) { free(ir); return -1; }
    fseek(f, 0, SEEK_END); fsz = ftell(f); fseek(f, 0, SEEK_SET);
    fb = (unsigned char *)malloc((unsigned long)fsz);
    nr = fread(fb, 1, (unsigned long)fsz, f); fclose(f);
    if ((long)nr != fsz) { free(fb); free(ir); return -1; }
    memcpy(&eh, fb, sizeof(eh));
    if (eh.e_ident[0] != ELFMAG0 || eh.e_type != ET_REL)
    { free(fb); free(ir); return -1; }

    sh = (Elf64_Shdr *)(fb + eh.e_shoff);
    si = (unsigned int)eh.e_shstrndx;
    oss = (char *)(fb + sh[si].sh_offset);
    oss_sz = (unsigned int)sh[si].sh_size;
    nss_sz = oss_sz + (unsigned int)sizeof(sn);
    nss = (char *)malloc(nss_sz);
    memcpy(nss, oss, oss_sz);
    memcpy(nss + oss_sz, sn, sizeof(sn));

    ir_off = (unsigned long)eh.e_shoff;
    ss_off = (ir_off + (unsigned long)ir_len + 7UL) & ~7UL;
    sh_off = (ss_off + nss_sz + 7UL) & ~7UL;

    memset(&ir_sh, 0, sizeof(ir_sh));
    ir_sh.sh_name = oss_sz; ir_sh.sh_type = SHT_PROGBITS;
    ir_sh.sh_offset = (u64)ir_off; ir_sh.sh_size = (u64)ir_len;
    ir_sh.sh_addralign = 1;
    memcpy(&ss_sh, &sh[si], sizeof(ss_sh));
    ss_sh.sh_offset = (u64)ss_off; ss_sh.sh_size = (u64)nss_sz;
    eh.e_shoff = (u64)sh_off; eh.e_shnum = eh.e_shnum + 1;

    f = fopen(obj_path, "wb");
    if (!f) { free(nss); free(fb); free(ir); return -1; }
    fwrite(&eh, sizeof(eh), 1, f);
    if (ir_off > sizeof(eh))
        fwrite(fb + sizeof(eh), 1, ir_off - sizeof(eh), f);
    fwrite(ir, 1, (unsigned long)ir_len, f);
    for (cur = ir_off + (unsigned long)ir_len; cur < ss_off; cur++)
        fputc(0, f);
    fwrite(nss, 1, nss_sz, f);
    for (cur = ss_off + nss_sz; cur < sh_off; cur++) fputc(0, f);
    for (i = 0; i < (int)eh.e_shnum - 1; i++) {
        if (i == (int)si) fwrite(&ss_sh, sizeof(Elf64_Shdr), 1, f);
        else fwrite(&sh[i], sizeof(Elf64_Shdr), 1, f);
    }
    fwrite(&ir_sh, sizeof(Elf64_Shdr), 1, f);
    fclose(f); free(nss); free(fb); free(ir);
    return 0;
}

/* ---- ELF .free_ir section read ---- */

struct ir_module *ir_read_from_elf(const char *obj_path, struct arena *a)
{
    FILE *f;
    unsigned char *fb;
    long fsz;
    unsigned long nr;
    Elf64_Ehdr *eh;
    Elf64_Shdr *sh;
    char *ss;
    int i, sn;

    f = fopen(obj_path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); fsz = ftell(f); fseek(f, 0, SEEK_SET);
    fb = (unsigned char *)malloc((unsigned long)fsz);
    nr = fread(fb, 1, (unsigned long)fsz, f); fclose(f);
    if ((long)nr != fsz) { free(fb); return NULL; }
    eh = (Elf64_Ehdr *)fb;
    if (eh->e_ident[0] != ELFMAG0 || eh->e_type != ET_REL)
    { free(fb); return NULL; }
    sh = (Elf64_Shdr *)(fb + eh->e_shoff);
    sn = (int)eh->e_shnum;
    ss = NULL;
    if (eh->e_shstrndx < (u16)sn)
        ss = (char *)(fb + sh[eh->e_shstrndx].sh_offset);
    if (!ss) { free(fb); return NULL; }
    for (i = 0; i < sn; i++) {
        if (strcmp(ss + sh[i].sh_name, ".free_ir") == 0) {
            struct ir_module *m;
            m = ir_deserialize(fb + sh[i].sh_offset, (int)sh[i].sh_size, a);
            free(fb); return m;
        }
    }
    free(fb); return NULL;
}
