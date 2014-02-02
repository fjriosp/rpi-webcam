#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jpeglib.h>
#include <sys/time.h>

#include "jpeg.h"

typedef struct IJPEGEncoder IJPEGEncoder;

struct IJPEGEncoder {
    JPEGEncoder e;
    Buffer* line;
};

typedef struct {
    struct jpeg_destination_mgr mgr;
    IJPEGEncoder* jctx;
} jpeg_destination_mem_mgr;

JPEGEncoder* jpeg_create_encoder() {
    IJPEGEncoder* jctx = calloc(1, sizeof (IJPEGEncoder));
    memset(jctx, 0, sizeof (IJPEGEncoder));
    jctx->line = buffer_create();
    return (JPEGEncoder*)jctx;
}

int jpeg_init(JPEGEncoder* encoder) {
    return 0;
}

int jpeg_destroy_encoder(JPEGEncoder* encoder) {
    IJPEGEncoder* ctx = (IJPEGEncoder*)encoder;
    if (ctx->line != NULL) {
        buffer_destroy(ctx->line);
        ctx->line = NULL;
    }

    free(ctx);

    return 0;
}

static void transform_yuv422_to_yuv444_line(unsigned char* yuyv, unsigned char* yuv, int width) {
    unsigned char y0, y1, u, v;
    int x;
    for (x = 0; x < width / 2; x++) {
        y0 = yuyv[4 * x + 0];
        u = yuyv[4 * x + 1];
        y1 = yuyv[4 * x + 2];
        v = yuyv[4 * x + 3];

        yuv[6 * x + 0] = y0;
        yuv[6 * x + 1] = u;
        yuv[6 * x + 2] = v;
        yuv[6 * x + 3] = y1;
        yuv[6 * x + 4] = u;
        yuv[6 * x + 5] = v;
    }
}

static void mem_init_destination(j_compress_ptr cinfo) {
    jpeg_destination_mem_mgr* dst = (jpeg_destination_mem_mgr*) cinfo->dest;
    IJPEGEncoder* jctx = (IJPEGEncoder*) dst->jctx;
    buffer_resize(jctx->e.output, 1024, 0);
    jctx->e.output->used = 0;
    cinfo->dest->next_output_byte = jctx->e.output->data;
    cinfo->dest->free_in_buffer = jctx->e.output->size;
}

static void mem_term_destination(j_compress_ptr cinfo) {
    jpeg_destination_mem_mgr* dst = (jpeg_destination_mem_mgr*) cinfo->dest;
    IJPEGEncoder* jctx = (IJPEGEncoder*) dst->jctx;
    jctx->e.output->used = jctx->e.output->size - cinfo->dest->free_in_buffer;
}

boolean mem_empty_output_buffer(j_compress_ptr cinfo) {
    jpeg_destination_mem_mgr* dst = (jpeg_destination_mem_mgr*) cinfo->dest;
    IJPEGEncoder * jctx = (IJPEGEncoder*) dst->jctx;
    size_t oldsize = jctx->e.output->size;
    buffer_resize(jctx->e.output, oldsize * 2, 0);
    cinfo->dest->free_in_buffer = oldsize;
    cinfo->dest->next_output_byte = jctx->e.output->data + oldsize;
    return TRUE;
}

void jpeg_custom_mem_dest(IJPEGEncoder* jctx, j_compress_ptr cinfo, jpeg_destination_mem_mgr* dst) {
    dst->jctx = jctx;
    cinfo->dest = (struct jpeg_destination_mgr*) dst;
    cinfo->dest->init_destination = mem_init_destination;
    cinfo->dest->term_destination = mem_term_destination;
    cinfo->dest->empty_output_buffer = mem_empty_output_buffer;
}

int jpeg_compress(JPEGEncoder* encoder) {
    IJPEGEncoder* jctx = (IJPEGEncoder*) encoder;
    int width = jctx->e.width;
    int height = jctx->e.height;
    int quality = jctx->e.quality;

    buffer_resize(jctx->line, 3 * width, 0);
    unsigned char* linebuf = jctx->line->data;
    unsigned char* inbuf = jctx->e.input->data;

    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    jpeg_destination_mem_mgr dst_mem;
    jpeg_custom_mem_dest(jctx, &cinfo, &dst_mem);

    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_YCbCr;

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);

    jpeg_start_compress(&cinfo, TRUE);

    while (cinfo.next_scanline < cinfo.image_height) {
        transform_yuv422_to_yuv444_line(inbuf + 2 * width * cinfo.next_scanline, linebuf, width);
        jpeg_write_scanlines(&cinfo, &linebuf, 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    return 0;
}

