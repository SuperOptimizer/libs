/* SPDX-License-Identifier: GPL-2.0 */
/* Stub scatterlist.h for free-cc kernel compilation testing */
#ifndef _LINUX_SCATTERLIST_H
#define _LINUX_SCATTERLIST_H

#include <linux/types.h>
#include <linux/bug.h>

struct scatterlist {
    unsigned long page_link;
    unsigned int offset;
    unsigned int length;
    dma_addr_t dma_address;
    unsigned int dma_length;
};

struct sg_table {
    struct scatterlist *sgl;
    unsigned int nents;
    unsigned int orig_nents;
};

#define SG_END     0x02UL
#define SG_CHAIN   0x01UL

#define sg_is_chain(sg) ((sg)->page_link & SG_CHAIN)
#define sg_is_last(sg)  ((sg)->page_link & SG_END)
#define sg_chain_ptr(sg) ((struct scatterlist *)((sg)->page_link & ~(SG_CHAIN | SG_END)))

static inline void sg_set_page(struct scatterlist *sg, struct page *page,
                               unsigned int len, unsigned int offset)
{
    sg->page_link = (unsigned long)page;
    sg->offset = offset;
    sg->length = len;
}

static inline void sg_set_buf(struct scatterlist *sg, const void *buf,
                              unsigned int buflen)
{
    (void)buf;
    sg->offset = 0;
    sg->length = buflen;
}

static inline struct scatterlist *sg_next(struct scatterlist *sg)
{
    if (sg_is_last(sg))
        return NULL;
    sg++;
    if (sg_is_chain(sg))
        sg = sg_chain_ptr(sg);
    return sg;
}

static inline void sg_init_table(struct scatterlist *sgl, unsigned int nents)
{
    unsigned int i;
    for (i = 0; i < nents; i++) {
        sgl[i].page_link = 0;
        sgl[i].offset = 0;
        sgl[i].length = 0;
    }
    sgl[nents - 1].page_link |= SG_END;
}

static inline void sg_init_one(struct scatterlist *sg, const void *buf,
                               unsigned int buflen)
{
    sg_init_table(sg, 1);
    sg_set_buf(sg, buf, buflen);
}

static inline void sg_mark_end(struct scatterlist *sg)
{
    sg->page_link |= SG_END;
    sg->page_link &= ~SG_CHAIN;
}

static inline void sg_unmark_end(struct scatterlist *sg)
{
    sg->page_link &= ~SG_END;
}

static inline dma_addr_t sg_dma_address(struct scatterlist *sg)
{
    return sg->dma_address;
}

static inline unsigned int sg_dma_len(struct scatterlist *sg)
{
    return sg->dma_length;
}

#define for_each_sg(sglist, sg, nr, __i) \
    for (__i = 0, sg = (sglist); __i < (nr); __i++, sg = sg_next(sg))

#define for_each_sgtable_sg(sgt, sg, i) \
    for_each_sg((sgt)->sgl, sg, (sgt)->orig_nents, i)

extern struct scatterlist *sg_alloc_table_chained(struct sg_table *table,
                                                  unsigned int nents,
                                                  struct scatterlist *first_chunk,
                                                  unsigned int nents_first_chunk);
extern void sg_free_table_chained(struct sg_table *table,
                                  unsigned int nents_first_chunk);
extern int sg_alloc_table(struct sg_table *, unsigned int, gfp_t);
extern void sg_free_table(struct sg_table *);

#endif /* _LINUX_SCATTERLIST_H */
