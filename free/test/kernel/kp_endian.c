/* Kernel pattern: endianness conversion */
#include <linux/types.h>
#include <linux/swab.h>
#include <linux/kernel.h>

/* Network byte order helpers */
static u16 my_htons(u16 val)
{
    return __swab16(val);
}

static u32 my_htonl(u32 val)
{
    return __swab32(val);
}

static u64 my_htonll(u64 val)
{
    return __swab64(val);
}

struct network_header {
    u16 src_port;
    u16 dst_port;
    u32 seq_num;
    u32 ack_num;
    u16 flags;
    u16 window;
};

static void header_to_network(struct network_header *hdr)
{
    hdr->src_port = my_htons(hdr->src_port);
    hdr->dst_port = my_htons(hdr->dst_port);
    hdr->seq_num = my_htonl(hdr->seq_num);
    hdr->ack_num = my_htonl(hdr->ack_num);
    hdr->flags = my_htons(hdr->flags);
    hdr->window = my_htons(hdr->window);
}

static void header_from_network(struct network_header *hdr)
{
    hdr->src_port = my_htons(hdr->src_port);
    hdr->dst_port = my_htons(hdr->dst_port);
    hdr->seq_num = my_htonl(hdr->seq_num);
    hdr->ack_num = my_htonl(hdr->ack_num);
    hdr->flags = my_htons(hdr->flags);
    hdr->window = my_htons(hdr->window);
}

static u32 checksum_header(const struct network_header *hdr)
{
    const u16 *p = (const u16 *)hdr;
    u32 sum = 0;
    int i;
    for (i = 0; i < (int)(sizeof(*hdr) / 2); i++)
        sum += p[i];
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return ~sum & 0xFFFF;
}

void test_endian(void)
{
    struct network_header hdr;
    u32 cksum;

    hdr.src_port = 8080;
    hdr.dst_port = 443;
    hdr.seq_num = 1000;
    hdr.ack_num = 0;
    hdr.flags = 0x02; /* SYN */
    hdr.window = 65535;

    header_to_network(&hdr);
    cksum = checksum_header(&hdr);
    header_from_network(&hdr);

    (void)cksum;
    (void)my_htonll;
}
