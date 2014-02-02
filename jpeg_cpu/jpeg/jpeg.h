#ifndef __JPEG_H__
#define __JPEG_H__

#include "../buffer/buffer.h"

typedef struct {
    int width;
    int height;
    int quality;
    Buffer* line;
    Buffer* output;
    Buffer* input;
} JPEGContext;

JPEGContext * create_jpeg_context();
int destroy_jpeg_context(JPEGContext * ctx);
int compress_yuyv_to_jpeg(JPEGContext * jctx);

#endif
