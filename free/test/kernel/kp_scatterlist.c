/* Kernel pattern: scatterlist I/O */
#include <linux/types.h>
#include <linux/scatterlist.h>
#include <linux/kernel.h>

#define MAX_SG_ENTRIES 16

struct dma_transfer {
    struct scatterlist sg[MAX_SG_ENTRIES];
    int nents;
    unsigned long total_len;
};

static void transfer_init(struct dma_transfer *xfer)
{
    sg_init_table(xfer->sg, MAX_SG_ENTRIES);
    xfer->nents = 0;
    xfer->total_len = 0;
}

static int transfer_add_buf(struct dma_transfer *xfer,
                            void *buf, unsigned int len)
{
    if (xfer->nents >= MAX_SG_ENTRIES)
        return -1;

    sg_set_buf(&xfer->sg[xfer->nents], buf, len);
    xfer->nents++;
    xfer->total_len += len;

    /* Update end marker */
    if (xfer->nents > 1)
        sg_unmark_end(&xfer->sg[xfer->nents - 2]);
    sg_mark_end(&xfer->sg[xfer->nents - 1]);

    return 0;
}

static unsigned long transfer_total_len(const struct dma_transfer *xfer)
{
    return xfer->total_len;
}

void test_scatterlist(void)
{
    struct dma_transfer xfer;
    char buf1[128];
    char buf2[256];
    char buf3[512];
    unsigned long total;

    transfer_init(&xfer);
    transfer_add_buf(&xfer, buf1, sizeof(buf1));
    transfer_add_buf(&xfer, buf2, sizeof(buf2));
    transfer_add_buf(&xfer, buf3, sizeof(buf3));

    total = transfer_total_len(&xfer);
    (void)total;
}
