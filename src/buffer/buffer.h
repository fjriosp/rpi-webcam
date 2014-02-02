#ifndef __BUFFER_H__
#define	__BUFFER_H__

#include <stdint.h>

typedef struct Buffer Buffer;

struct Buffer {
    uint8_t* data;
    uint32_t size;
    uint32_t used;
};

Buffer* buffer_create();
int buffer_resize(Buffer* b, int size, int force);
int buffer_copy(Buffer* d, const Buffer* s);
int buffer_destroy(Buffer* b);

#endif

