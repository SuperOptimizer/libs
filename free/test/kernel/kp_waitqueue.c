/* Kernel pattern: wait queue usage */
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/kernel.h>

struct io_channel {
    wait_queue_head_t wait;
    int data_ready;
    unsigned long bytes_available;
    int error_code;
};

static void channel_init(struct io_channel *ch)
{
    init_waitqueue_head(&ch->wait);
    ch->data_ready = 0;
    ch->bytes_available = 0;
    ch->error_code = 0;
}

static void channel_signal_data(struct io_channel *ch, unsigned long bytes)
{
    ch->bytes_available = bytes;
    ch->data_ready = 1;
    wake_up(&ch->wait);
}

static void channel_signal_error(struct io_channel *ch, int error)
{
    ch->error_code = error;
    ch->data_ready = 1;
    wake_up_all(&ch->wait);
}

static int channel_has_data(struct io_channel *ch)
{
    return ch->data_ready;
}

void test_waitqueue(void)
{
    struct io_channel ch;

    channel_init(&ch);
    channel_signal_data(&ch, 1024);

    if (channel_has_data(&ch))
        ch.data_ready = 0;

    channel_signal_error(&ch, -5);
}
