/* Kernel pattern: raw annotation spellings */
#include <linux/compiler_types.h>
#include <linux/types.h>

struct percpu_page {
    unsigned long flags;
};

static __always_inline void __section(".init.text")
annotated_init(struct percpu_page *page)
{
    page->flags = (__force unsigned long)1;
}

static struct percpu_page __percpu * __percpu *percpu_chain(
    struct percpu_page __percpu * __percpu *slot)
{
    return slot;
}

void test_annotation_tokens(struct percpu_page __percpu * __percpu *slot)
{
    struct percpu_page page;
    struct percpu_page __percpu * __percpu *tmp;

    page.flags = 0;
    annotated_init(&page);
    tmp = percpu_chain(slot);
    (void)tmp;
}
