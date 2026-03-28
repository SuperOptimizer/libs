/* Kernel pattern: arm64 ALTERNATIVE_CB immediate operand. */

#define __stringify_1(x) #x
#define __stringify(x) __stringify_1(x)
#define __always_inline __attribute__((always_inline)) inline

#define ALTINSTR_ENTRY_CB(cpucap, cb) \
    " .hword " __stringify(cpucap) "\n"
#define ALTERNATIVE_CB(oldinstr, cpucap, cb) \
    ALTINSTR_ENTRY_CB(cpucap, cb) \
    oldinstr

static inline void alt_cb_patch_nops(void)
{
}

static __always_inline int alternative_has_cap_likely(const unsigned long cpucap)
{
    asm goto(ALTERNATIVE_CB("b %l[l_no]", %[cpucap], alt_cb_patch_nops)
             : : [cpucap] "i" (cpucap) : : l_no);
    return 1;
l_no:
    return 0;
}

int f(void)
{
    return alternative_has_cap_likely(123);
}
