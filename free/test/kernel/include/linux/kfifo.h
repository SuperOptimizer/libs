/* SPDX-License-Identifier: GPL-2.0 */
/* Stub kfifo.h for free-cc kernel compilation testing */
#ifndef _LINUX_KFIFO_H
#define _LINUX_KFIFO_H

#include <linux/types.h>
#include <linux/spinlock.h>

struct __kfifo {
    unsigned int in;
    unsigned int out;
    unsigned int mask;
    unsigned int esize;
    void *data;
};

#define __STRUCT_KFIFO_COMMON(datatype, recsize, ptrtype) \
    union { \
        struct __kfifo kfifo; \
        datatype *type; \
        const datatype *const_type; \
        char (*rectype)[recsize]; \
        ptrtype *ptr; \
        ptrtype const *ptr_const; \
    }

#define __STRUCT_KFIFO(type, size, recsize, ptrtype) \
    { \
        __STRUCT_KFIFO_COMMON(type, recsize, ptrtype); \
        type buf[((size < 2) || (size & (size - 1))) ? -1 : size]; \
    }

#define STRUCT_KFIFO(type, size) \
    struct __STRUCT_KFIFO(type, size, 0, type)

#define __STRUCT_KFIFO_PTR(type, recsize, ptrtype) \
    { \
        __STRUCT_KFIFO_COMMON(type, recsize, ptrtype); \
        type buf[0]; \
    }

#define STRUCT_KFIFO_PTR(type) \
    struct __STRUCT_KFIFO_PTR(type, 0, type)

struct kfifo {
    struct __kfifo kfifo;
    unsigned char *buf;
};

struct kfifo_rec_ptr_1 {
    struct __kfifo kfifo;
    unsigned char *buf;
};

struct kfifo_rec_ptr_2 {
    struct __kfifo kfifo;
    unsigned char *buf;
};

extern int __kfifo_alloc(struct __kfifo *fifo, unsigned int size,
                         size_t esize, gfp_t gfp_mask);
extern void __kfifo_free(struct __kfifo *fifo);
extern int __kfifo_init(struct __kfifo *fifo, void *buffer,
                        unsigned int size, size_t esize);
extern unsigned int __kfifo_in(struct __kfifo *fifo,
                               const void *buf, unsigned int len);
extern unsigned int __kfifo_out(struct __kfifo *fifo,
                                void *buf, unsigned int len);
extern unsigned int __kfifo_out_peek(struct __kfifo *fifo,
                                     void *buf, unsigned int len);
extern unsigned int __kfifo_in_r(struct __kfifo *fifo,
                                 const void *buf, unsigned int len,
                                 size_t recsize);
extern unsigned int __kfifo_out_r(struct __kfifo *fifo,
                                  void *buf, unsigned int len,
                                  size_t recsize);
extern unsigned int __kfifo_len_r(struct __kfifo *fifo, size_t recsize);
extern void __kfifo_skip_r(struct __kfifo *fifo, size_t recsize);
extern unsigned int __kfifo_out_peek_r(struct __kfifo *fifo,
                                       void *buf, unsigned int len,
                                       size_t recsize);
extern unsigned int __kfifo_max_r(unsigned int len, size_t recsize);

extern int __kfifo_to_user(struct __kfifo *fifo,
                           void __user *to, unsigned long len,
                           unsigned int *copied);
extern int __kfifo_from_user(struct __kfifo *fifo,
                             const void __user *from, unsigned long len,
                             unsigned int *copied);
extern int __kfifo_to_user_r(struct __kfifo *fifo,
                             void __user *to, unsigned long len,
                             unsigned int *copied, size_t recsize);
extern int __kfifo_from_user_r(struct __kfifo *fifo,
                               const void __user *from, unsigned long len,
                               unsigned int *copied, size_t recsize);

extern unsigned int __kfifo_dma_in_prepare(struct __kfifo *fifo,
                                           struct scatterlist *sgl,
                                           int nents, unsigned int len);
extern unsigned int __kfifo_dma_out_prepare(struct __kfifo *fifo,
                                            struct scatterlist *sgl,
                                            int nents, unsigned int len);
extern unsigned int __kfifo_dma_in_prepare_r(struct __kfifo *fifo,
                                             struct scatterlist *sgl,
                                             int nents, unsigned int len,
                                             size_t recsize);
extern unsigned int __kfifo_dma_out_prepare_r(struct __kfifo *fifo,
                                              struct scatterlist *sgl,
                                              int nents, unsigned int len,
                                              size_t recsize);
extern void __kfifo_dma_in_finish_r(struct __kfifo *fifo,
                                    unsigned int len, size_t recsize);

#endif /* _LINUX_KFIFO_H */
