#ifndef __JPEG_H__
#define __JPEG_H__

#include "../buffer/buffer.h"

typedef struct JPEGEncoder JPEGEncoder;

struct JPEGEncoder {
    int width;
    int height;
    int quality;
    Buffer* output;
    Buffer* input;
};

JPEGEncoder* jpeg_create_encoder();
int jpeg_init(JPEGEncoder* encoder);
int jpeg_compress(JPEGEncoder* encoder);
int jpeg_destroy_encoder(JPEGEncoder* encoder);

#endif
