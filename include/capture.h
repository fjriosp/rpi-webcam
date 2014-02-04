#ifndef __CAPTURE_H__
#define __CAPTURE_H__

#include "buffer.h"

typedef struct Capture Capture;

struct Capture {
    char dev[64];
    int width;
    int height;
};

Capture * capture_create();
int capture_init(Capture *c);
int capture_flush(Capture *c);
Buffer* capture_grab(Capture *c);
int capture_release_buffer(Capture* c, Buffer* b);
int capture_destroy(Capture *c);

#endif
