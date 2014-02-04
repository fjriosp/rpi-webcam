#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "buffer.h"
#include "log.h"

Buffer* buffer_create() {
    Buffer* b = malloc(sizeof (Buffer));
    if (b == NULL) {
        LOG_ERROR("Creating Buffer");
        return NULL;
    }
    memset(b, 0, sizeof (Buffer));
    return b;
}

int buffer_resize(Buffer* b, int size, int force) {
    if (b == NULL) {
        LOG_ERROR("Null Buffer!");
        return -1;
    }

    if (b->size >= size && !force)
        return 0;

    uint8_t* ndata = realloc(b->data, size);
    if (size > 0 && ndata == NULL) {
        LOG_ERROR("Reallocating Buffer");
        return -1;
    }

    b->data = ndata;
    b->size = size;
    if (b->size < b->used) {
        b->used = b->size;
    }

    return 0;
}

int buffer_copy(Buffer* d, const Buffer* s) {
    if (0 > buffer_resize(d, s->used, 0)) {
        LOG_ERROR("Error Resizing Buffer");
        return -1;
    }
    d->used = s->used;
    memccpy(d->data, s->data, s->used, 1);
    return 0;
}

int buffer_destroy(Buffer* b) {
    if (b->data != NULL) {
        free(b->data);
        b->data = NULL;
    }

    free(b);

    return 0;
}
