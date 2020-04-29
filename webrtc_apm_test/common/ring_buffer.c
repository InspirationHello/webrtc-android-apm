#include "ring_buffer.h"

#include <stdlib.h>
#include <string.h>

#ifndef unref_param
#define unref_param(...) ((void)(__VA_ARGS__))
#endif

#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif


struct ring_buffer_ {
    uint8_t           *data;
    uint32_t           size, size_mask; /* size must be pow of two */
    volatile uint32_t  rpos;
    volatile uint32_t  wpos;
};

static uint32_t roundup_pow_of_two(uint32_t x)
{
    x--;

    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;

    x++;

    return x;
}

ring_buffer_t* ring_buffer_create()
{
    ring_buffer_t *rbuf;
    
    rbuf = (ring_buffer_t*)malloc(sizeof(ring_buffer_t));
    if(rbuf){
        memset(rbuf, 0, sizeof(*rbuf));
    }
    
    return rbuf;
}

void ring_buffer_destroy(ring_buffer_t *rbuf)
{
    free(rbuf);
}

int ring_buffer_init(ring_buffer_t *rbuf, uint8_t *data, size_t len)
{
    if (roundup_pow_of_two(len) != len) {
        // assert(0);
        return -1;
    }

    rbuf->rpos = rbuf->wpos = 0;
    rbuf->data = data;
    rbuf->size = len;
    rbuf->size_mask = rbuf->size - 1;

    return 0;
}

uint32_t ring_buffer_free(ring_buffer_t *rbuf)
{
    int32_t free;

    free = rbuf->rpos - rbuf->wpos;
    if (free <= 0)
        free += rbuf->size;

    return (uint32_t)(free - 1);
}


uint32_t ring_buffer_avail(ring_buffer_t *rbuf)
{
    int32_t avail;

    avail = rbuf->wpos - rbuf->rpos;
    if (avail < 0)
        avail += rbuf->size;

    return (uint32_t)avail;
}

int ring_buffer_empty(ring_buffer_t *rbuf)
{
    return (rbuf->rpos == rbuf->wpos);
}

int ring_buffer_full(ring_buffer_t *rbuf)
{
    return ring_buffer_avail(rbuf) == rbuf->size;
}

void ring_buffer_flush(ring_buffer_t *rbuf)
{
    rbuf->rpos = rbuf->wpos;
}

int ring_buffer_write(ring_buffer_t *rbuf, const uint8_t* buf, size_t leng)
{
    size_t to_transfer, chunk;
    uint32_t wpos, start, copied = 0;

    to_transfer = ring_buffer_free(rbuf);
    to_transfer = min(to_transfer, leng);

    wpos = rbuf->wpos;

    while (to_transfer) {
        start = wpos & rbuf->size_mask;
        chunk = min(to_transfer, rbuf->size - start);

        memcpy(rbuf->data + start, buf + copied, chunk);

        to_transfer -= chunk;
        copied      += chunk;
        wpos        += chunk;
    }

    rbuf->wpos = wpos & rbuf->size_mask;

    if(wpos != rbuf->wpos){
        #include "w_log.h"

        // LOGW("!!! circle buffer write wrapped !!!");
    }

    return copied;
}

int ring_buffer_consume(ring_buffer_t *rbuf, uint8_t *dest_buf, ssize_t leng, 
    process_data_pft callback, void *args)
{
    size_t to_transfer, chunk;
    uint32_t rpos, start, copied = 0;
    int32_t written = 0;
    uint8_t *dest = dest_buf;

    to_transfer = ring_buffer_avail(rbuf);
    to_transfer = min(to_transfer, leng);

    rpos = rbuf->rpos;

    if(!callback){
        return to_transfer;
    }

    while (to_transfer) {
        start = rpos & rbuf->size_mask;
        chunk = min(to_transfer, rbuf->size - start);

        written = callback(rbuf->data + start, dest, chunk, args);
        if (written <= 0) {
            break;
        }

        to_transfer -= written;
        copied      += written;
        rpos        += written;
        dest        += written;
    }

    rbuf->rpos = rpos & rbuf->size_mask;

    return copied;
}

static ssize_t copy_data_proc(uint8_t *src, uint8_t *dest, ssize_t size, void *args)
{
    unref_param(args);

    memcpy(dest, src, size);
    
    return size;
}

int ring_buffer_read(ring_buffer_t *rbuf, uint8_t* buf, size_t leng)
{
    return ring_buffer_consume(rbuf, buf, leng, copy_data_proc, NULL);
}


