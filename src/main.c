#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

#include "buffer.h"
#include "capture.h"
#include "jpeg.h"
#include "log.h"

typedef struct MainContext {
    Capture* cctx;
    JPEGEncoder *jctx;
    int exit;
    int client;
    sem_t full;
    sem_t empty;

    Buffer* cbuffer;
    Buffer* jpeg_buffer;
} MainContext;

void swap_buffers(MainContext * mctx) {
    Buffer* tmp;

    tmp = mctx->jctx->output;
    mctx->jctx->output = mctx->jpeg_buffer;
    mctx->jpeg_buffer = tmp;
}

void *producer(void * arg) {
    logger_set_thread_name("Prod");
    LOG_TRACE("Producer starts");
    MainContext * mctx = (MainContext *) arg;
    struct timeval t;

    while (1) {
        // Wait to dequeue
        LOG_TRACE("Wait buffer empty");
        gettimeofday(&t, NULL);
        sem_wait(&mctx->empty);
        LOG_INFO_TIME(&t, "Wait buffer empty");

        // Exit condition
        if (mctx->exit) break;
        // Take a frame
        LOG_TRACE("Grab frame");
        gettimeofday(&t, NULL);
        Buffer* frame = capture_grab(mctx->cctx);
        if (frame == NULL) {
            // Error repeat the last frame
            LOG_ERROR("Error grabbing a frame");
            sem_post(&mctx->full);
            continue;
        }
        LOG_INFO_TIME(&t, "Grab frame");

        LOG_TRACE("Frame size %lu", frame->used);

        //JPEG Compress
        LOG_TRACE("JPEG Compress");
        gettimeofday(&t, NULL);
        mctx->jctx->input = frame;

        // Write out the raw image
        /*
        FILE* f = fopen("test.yuyv", "wb");
        fwrite(mctx->jctx->input->data, 1, mctx->jctx->input->used, f);
        fclose(f);
         */

        jpeg_compress(mctx->jctx);
        LOG_INFO_TIME(&t, "JPEG Compress");

        LOG_TRACE("JPEG size %lu", mctx->jctx->output->used);

        // Notify buffer full
        LOG_TRACE("Notify buffer available");
        sem_post(&mctx->full);

        // Release capture buffer
        if (0 > capture_release_buffer(mctx->cctx, frame)) {
            LOG_ERROR("Error releasing buffer");
            // Ignore
        }
    }

    LOG_TRACE("Producer exit");
    pthread_exit(0);
}

int main(int ac, char** av) {
    logger_init(LEVEL_TRACE, stdout);
    logger_set_thread_name("Main");

    LOG_INFO("rpi-webcam");

    MainContext mctx;
    memset(&mctx, 0, sizeof (mctx));

    // JPEG Buffer
    mctx.jpeg_buffer = buffer_create();

    // Semaphores to sync threads
    LOG_TRACE("Initialize semaphores");
    sem_init(&mctx.full, 0, 0);
    sem_init(&mctx.empty, 0, 1);

    // Capture context
    LOG_TRACE("Create Capture Context");
    mctx.cctx = capture_create();
    mctx.cctx->width = 16000;
    mctx.cctx->height = 12000;

    // Init the webcam
    LOG_INFO("Initialize Capture");
    if (0 != capture_init(mctx.cctx)) {
        return -1;
    }

    // JPEG context
    LOG_TRACE("Create JPEG Context");
    mctx.jctx = jpeg_create_encoder();
    mctx.jctx->width = mctx.cctx->width;
    mctx.jctx->height = mctx.cctx->height;
    mctx.jctx->quality = 70;
    mctx.jctx->output = buffer_create();

    jpeg_init(mctx.jctx);

    // Start capture thread
    LOG_TRACE("Launch producer thread");
    pthread_t prod;
    pthread_create(&prod, NULL, &producer, &mctx);

    // Sockets
    LOG_TRACE("Create Socket");
    int sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        LOG_ERROR("Create Socket");
        return -1;
    }

    int val = 1;
    if (0 != setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &val, sizeof (val))) {
        LOG_ERROR("Configure Socket SO_REUSEADDR");
        return -1;
    }

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof (saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = htonl(INADDR_ANY);
    saddr.sin_port = htons(9000);

    LOG_TRACE("Bind Socket");
    if (0 != bind(sock, (struct sockaddr*) &saddr, sizeof (saddr))) {
        LOG_ERROR("Error binding Socket");
        return -1;
    }

    LOG_TRACE("Listen Socket");
    if (0 != listen(sock, 10)) {
        LOG_ERROR("Error listening Socket");
        return -1;
    }

    int r;
    unsigned char cmd;
    time_t last = time(NULL);
    while (!mctx.exit) {
        LOG_INFO("Waiting a connection...");
        int client = accept(sock, NULL, NULL);
        if (client < 0) {
            LOG_ERROR("Error accepting connection");
            return -1;
        }

        LOG_INFO("Connection established");
        mctx.client = client;

        r = read(mctx.client, &cmd, sizeof (unsigned char));
        if (r <= 0) {
            LOG_ERROR("Error reading command");
        } else if (cmd == 'q') {
            LOG_INFO("Exit command received");
            mctx.exit = 1;
            // Signal Producer (TO FINISH)
            LOG_TRACE("Signaling producer thread to finish him");
            sem_post(&mctx.empty);
        } else if (cmd == 'f') {
            LOG_INFO("Frame command received");
            time_t now = time(NULL);
            if (now - last > 60) {
                LOG_INFO("New connection after %d seconds idle", now - last);

                // Flush capture buffers
                LOG_INFO("Flush V4L2 buffers");
                capture_flush(mctx.cctx);
                // The next frame has been already processed by the producer
                LOG_INFO("Skip old frame");
                sem_wait(&mctx.full);
                LOG_TRACE("Signaling producer thread to grab a new frame");
                sem_post(&mctx.empty);
            }
            last = now;

            // Wait a frame
            LOG_TRACE("Waiting for a frame buffer filled");
            sem_wait(&mctx.full);
            // Swap the buffers to generate a new frame while sending
            swap_buffers(&mctx);
            // Signal Producer
            LOG_TRACE("Signaling producer thread to fill the buffer again");
            sem_post(&mctx.empty);

            LOG_TRACE("Sending frame");
            ssize_t w = write(mctx.client, mctx.jpeg_buffer->data, mctx.jpeg_buffer->used);
            LOG_TRACE("%ld bytes sent", w);
        } else {
            LOG_WARN("Command '%c' unknown", cmd);
        }

        LOG_INFO("Closing connection");
        close(client);
    }

    // Wait the producer to finish
    LOG_TRACE("Waiting producer to finish");
    pthread_join(prod, NULL);

    // Cleanup
    LOG_INFO("Cleanup");
    LOG_TRACE("Close socket");
    close(sock);

    LOG_TRACE("Free semaphores");
    sem_destroy(&mctx.full);
    sem_destroy(&mctx.empty);

    LOG_TRACE("Free buffers");
    if (mctx.jpeg_buffer != NULL) {
        buffer_destroy(mctx.jpeg_buffer);
        mctx.jpeg_buffer = NULL;
    }

    if (mctx.jctx->output != NULL) {
        buffer_destroy(mctx.jctx->output);
        mctx.jctx->output = NULL;
    }

    LOG_TRACE("Free capture context");
    if (0 != capture_destroy(mctx.cctx)) {
        LOG_WARN("Error cleaning capture context");
        return -1;
    }

    LOG_TRACE("Free JPEG context");
    if (0 != jpeg_destroy_encoder(mctx.jctx)) {
        LOG_WARN("Error cleaning JPEG context");
        return -1;
    }

    LOG_TRACE("Close logger");
    logger_destroy();

    exit(0);
}

