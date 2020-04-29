#ifndef RING_BUFFER_H_
#define RING_BUFFER_H_

#include <stdint.h>

/* only for one writer thread and one reader thread */

typedef struct ring_buffer_ ring_buffer_t;

typedef ssize_t (*process_data_pft)(uint8_t *src, uint8_t *dest, ssize_t size, void *args);

ring_buffer_t* ring_buffer_create();
void ring_buffer_destroy(ring_buffer_t *rbuf);

int ring_buffer_init(ring_buffer_t *rbuf, uint8_t *data, size_t len);

uint32_t ring_buffer_free(ring_buffer_t *rbuf);
uint32_t ring_buffer_avail(ring_buffer_t *rbuf);

int ring_buffer_empty(ring_buffer_t *rbuf);
int ring_buffer_full(ring_buffer_t *rbuf);

void ring_buffer_flush(ring_buffer_t *rbuf);

int ring_buffer_write(ring_buffer_t *rbuf, const uint8_t* buf, size_t leng);
int ring_buffer_read(ring_buffer_t *rbuf, uint8_t* buf, size_t leng);

int ring_buffer_consume(ring_buffer_t *rbuf, uint8_t *dest_buf, ssize_t leng, 
    process_data_pft callback, void *args);

#endif
