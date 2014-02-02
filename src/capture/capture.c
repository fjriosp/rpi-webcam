#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include "capture.h"
#include "../log/log.h"

typedef enum {
    UNINITIALIZED,
    INITIALIZING,
    INITIALIZED,
    STARTING,
    IDLE,
    GRABBING,
    STOPPING,
    DESTROY
} CaptureStatus;

typedef struct ICapture ICapture;

struct ICapture {
    Capture c;
    int fd;
    int nbuf;
    Buffer** cbuffer;
    CaptureStatus status;
};

static int xioctl(int fd, int request, void *arg) {
    int r;
    do r = ioctl(fd, request, arg); while (-1 == r && EINTR == errno);
    return r;
}

Capture* capture_create() {
    LOG_TRACE("Create Capture Context");
    ICapture* ic = calloc(1, sizeof (ICapture));
    memset(ic, 0, sizeof (ICapture));
    strcpy(ic->c.dev, "/dev/video0");
    ic->c.width = 320;
    ic->c.height = 240;
    ic->nbuf = 3;
    ic->fd = -1;
    ic->status = UNINITIALIZED;
    return (Capture*) ic;
}

int capture_start(Capture* c) {
    ICapture* ic = (ICapture*) c;

    LOG_TRACE("Start Capture");
    if (ic->status != INITIALIZED) return -1;
    ic->status = STARTING;

    struct v4l2_buffer buf;
    // Start Capture
    memset(&buf, 0, sizeof (struct v4l2_buffer));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == xioctl(ic->fd, VIDIOC_STREAMON, &buf.type)) {
        LOG_ERROR("Start Capture");
        return -1;
    }

    ic->status = IDLE;
    return 0;
}

int capture_init(Capture* c) {
    ICapture* ic = (ICapture*) c;

    LOG_TRACE("Init Capture");
    if (ic->status != UNINITIALIZED) return -1;
    ic->status = INITIALIZING;

    // Buffers
    LOG_TRACE("Allocate Buffers");
    ic->cbuffer = (Buffer**) calloc(ic->nbuf, sizeof (Buffer*));
    if (ic->cbuffer == NULL) {
        LOG_ERROR("Allocating buffers array");
        return -1;
    }
    memset(ic->cbuffer, 0, ic->nbuf * sizeof (Buffer*));

    int i;
    for (i = 0; i < ic->nbuf; i++) {
        ic->cbuffer[i] = buffer_create();
        if (ic->cbuffer[i] == NULL) {
            LOG_ERROR("Allocating Buffer[%d]", i);
            return -1;
        }
    }

    // Open Device
    LOG_TRACE("Open device: %s", ic->c.dev);
    ic->fd = open(ic->c.dev, O_RDWR);
    if (ic->fd == -1) {
        // couldn't find capture device
        LOG_ERROR("Opening Video device");
        return -1;
    }

    // Capabilities
    LOG_TRACE("Query Capabilities");
    struct v4l2_capability caps;
    memset(&caps, 0, sizeof (struct v4l2_capability));
    if (-1 == xioctl(ic->fd, VIDIOC_QUERYCAP, &caps)) {
        LOG_ERROR("Querying Capabilites");
        return -1;
    }

    // Format
    LOG_TRACE("Set Format");
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof (struct v4l2_format));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = ic->c.width;
    fmt.fmt.pix.height = ic->c.height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;

    if (-1 == xioctl(ic->fd, VIDIOC_S_FMT, &fmt)) {
        LOG_ERROR("Setting Pixel Format");
        return -1;
    }

    ic->c.width = fmt.fmt.pix.width;
    ic->c.height = fmt.fmt.pix.height;
    LOG_TRACE("Width=%5d, Height=%5d", ic->c.width, ic->c.height);

    // Request Buffer
    LOG_TRACE("Request %d Buffers", ic->nbuf);
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof (struct v4l2_requestbuffers));
    req.count = ic->nbuf;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (-1 == xioctl(ic->fd, VIDIOC_REQBUFS, &req)) {
        LOG_ERROR("Requesting Buffer");
        return -1;
    }

    struct v4l2_buffer buf;
    for (i = 0; i < ic->nbuf; i++) {
        // Query Buffer
        LOG_TRACE("Query Buffer[%d]", i);
        memset(&buf, 0, sizeof (struct v4l2_buffer));
        buf.index = i;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (-1 == xioctl(ic->fd, VIDIOC_QUERYBUF, &buf)) {
            LOG_ERROR("Querying Buffer");
            LOG_DEBUG("buf.index=%d", buf.index);
            LOG_DEBUG("buf.type=%d", buf.type);
            LOG_DEBUG("buf.memory=%d", buf.memory);
            return -1;
        }

        LOG_TRACE("MMAP Buffer[%d]", i);
        ic->cbuffer[i]->data = mmap(NULL, buf.length, PROT_READ, MAP_SHARED, ic->fd, buf.m.offset);
        ic->cbuffer[i]->size = buf.length;

        // Queue Buffer
        LOG_TRACE("Queue Buffer[%d]", i);
        memset(&buf, 0, sizeof (struct v4l2_buffer));
        buf.index = i;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (-1 == ioctl(ic->fd, VIDIOC_QBUF, &buf)) {
            LOG_ERROR("Queue Buffer");
            return -1;
        }
    }

    ic->status = INITIALIZED;

    LOG_TRACE("Starting Capture");
    if (0 != capture_start(c)) {
        LOG_ERROR("Start Capture");
        return -1;
    }
    return 0;
}

int capture_flush(Capture* c) {
    ICapture* ic = (ICapture*) c;

    LOG_TRACE("Flush Buffers");
    if (ic->status != IDLE) return -1;
    ic->status = GRABBING;

    struct v4l2_buffer buf;
    int idx;
    int i;
    for (i = 0; i < ic->nbuf; i++) {
        // Wait Frame
        LOG_TRACE("Waiting Frame Ready");
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(ic->fd, &fds);
        struct timeval tv = {0};
        tv.tv_sec = 2;
        int r = select(ic->fd + 1, &fds, NULL, NULL, &tv);
        if (-1 == r) {
            LOG_ERROR("Waiting for Frame");
            return -1;
        }

        // Dequeue
        LOG_TRACE("Dequeue buffer");
        memset(&buf, 0, sizeof (struct v4l2_buffer));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (-1 == xioctl(ic->fd, VIDIOC_DQBUF, &buf)) {
            LOG_ERROR("Retrieving Frame");
            return -1;
        }
        LOG_TRACE("Dequeued buffer[%d]", buf.index);

        idx = buf.index;

        memset(&buf, 0, sizeof (struct v4l2_buffer));
        buf.index = idx;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        LOG_TRACE("Requeue Buffer[%d]", buf.index);
        if (-1 == ioctl(ic->fd, VIDIOC_QBUF, &buf)) {
            LOG_ERROR("Queue Buffer");
            return -1;
        }
    }

    ic->status = IDLE;

    return 0;
}

Buffer* capture_grab(Capture* c) {
    ICapture* ic = (ICapture*) c;

    LOG_TRACE("Grab Frame");
    if (ic->status != IDLE) return NULL;
    ic->status = GRABBING;

    // Wait Frame
    LOG_TRACE("Waiting Frame Ready");
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(ic->fd, &fds);
    struct timeval tv = {0};
    tv.tv_sec = 2;
    int r = select(ic->fd + 1, &fds, NULL, NULL, &tv);
    if (-1 == r) {
        LOG_ERROR("Waiting for Frame");
        return NULL;
    }

    // Dequeue
    struct v4l2_buffer buf;
    LOG_TRACE("Dequeue buffer");
    memset(&buf, 0, sizeof (struct v4l2_buffer));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (-1 == xioctl(ic->fd, VIDIOC_DQBUF, &buf)) {
        LOG_ERROR("Retrieving Frame");
        return NULL;
    }

    LOG_TRACE("Dequeued buffer[%d]", buf.index);
    ic->cbuffer[buf.index]->used = buf.bytesused;

    ic->status = IDLE;

    return ic->cbuffer[buf.index];
}

int capture_release_buffer(Capture* c, Buffer* b) {
    ICapture* ic = (ICapture*) c;

    int i = 0;
    while (i < ic->nbuf && ic->cbuffer[i] != b) {
        i++;
    }

    if (i >= ic->nbuf) {
        LOG_ERROR("Buffer Unknown");
        return -1;
    }

    // reQueue Buffer
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof (struct v4l2_buffer));
    buf.index = i;
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    LOG_TRACE("Requeue Buffer[%d]", buf.index);
    if (-1 == ioctl(ic->fd, VIDIOC_QBUF, &buf)) {
        LOG_ERROR("Queue Buffer");
        return -1;
    }

    return 0;
}

int capture_stop(Capture* c) {
    ICapture* ic = (ICapture*) c;

    LOG_TRACE("Stop Capture");
    if (ic->status != IDLE) return -1;
    ic->status = STOPPING;

    struct v4l2_buffer buf;
    // Capture Stop
    memset(&buf, 0, sizeof (struct v4l2_buffer));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == xioctl(ic->fd, VIDIOC_STREAMOFF, &buf.type)) {
        LOG_ERROR("Stop Capture");
        return -1;
    }

    ic->status = INITIALIZED;

    return 0;
}

int capture_destroy(Capture* c) {
    ICapture* ic = (ICapture*) c;

    LOG_TRACE("Destroy Capture Object");
    if (0 != capture_stop(c)) {
        LOG_ERROR("Stop Capture");
        return -1;
    }

    if (ic->status != INITIALIZED) return -1;
    ic->status = DESTROY;

    // Free Buffers
    LOG_TRACE("Free Buffers");
    int i;
    if (ic->cbuffer != NULL) {
        for (i = 0; i < ic->nbuf; i++) {
            if (ic->cbuffer[i] != NULL) {
                if (ic->cbuffer[i]->data != NULL) {
                    if (-1 == munmap(ic->cbuffer[i]->data, ic->cbuffer[i]->size)) {
                        LOG_ERROR("Unmap Buffer");
                        return -1;
                    }
                    ic->cbuffer[i]->data = NULL;
                }

                if (0 > buffer_destroy(ic->cbuffer[i])) {
                    LOG_ERROR("Free Buffer");
                    return -1;
                }
                ic->cbuffer[i] = NULL;
            }
        }
        free(ic->cbuffer);
        ic->cbuffer = NULL;
    }

    // Close Device
    LOG_TRACE("Close Device");
    if (ic->fd > 0) {
        if (-1 == close(ic->fd)) {
            LOG_ERROR("Close Device");
            return -1;
        }
        ic->fd = -1;
    }

    LOG_TRACE("Free Object");
    free(ic);

    return 0;
}

