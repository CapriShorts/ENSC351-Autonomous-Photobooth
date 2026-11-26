/* V4L2 video picture grabber
   Copyright (C) 2009 Mauro Carvalho Chehab <mchehab@infradead.org>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   Modified by Derek Molloy (www.derekmolloy.ie)
   Modified to change resolution details and set paths for the Beaglebone.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <libv4l2.h>
#include <pthread.h>

#include "grabber.h"

#define CLEAR(x) memset(&(x), 0, sizeof(x))

#define CAMERA_DEVICE "/dev/video3"
#define CAMERA_WIDTH 1280
#define CAMERA_HEIGHT 960
#define WARMUP_FRAMES 20

struct buffer {
    void *start;
    size_t length;
};

static pthread_mutex_t captureMutex =
    PTHREAD_MUTEX_INITIALIZER;

static bool captureActive = false;

static int xioctl(int fd, unsigned long request, void *arg)
{
    int result;

    do {
        result = v4l2_ioctl(fd, request, arg);
    } while (result == -1 && errno == EINTR);

    return result;
}

static void *grabber_thread(void *arg)
{
    (void)arg;

    struct v4l2_format fmt;
    struct v4l2_buffer buf;
    struct v4l2_requestbuffers req;

    enum v4l2_buf_type type =
        V4L2_BUF_TYPE_VIDEO_CAPTURE;

    struct buffer *buffers = NULL;

    unsigned int n_buffers = 0;
    unsigned int i;

    int fd = -1;
    bool streaming = false;

    fd_set fds;
    struct timeval tv;

    char out_name[256];
    FILE *fout;

    printf("Initializing camera...\n");

    // Open camera
    fd = v4l2_open(CAMERA_DEVICE,
                   O_RDWR | O_NONBLOCK, 0);

    if (fd < 0) {
        perror("Cannot open camera");
        goto cleanup;
    }

    // Configure image format
    CLEAR(fmt);

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = CAMERA_WIDTH;
    fmt.fmt.pix.height = CAMERA_HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;

    if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("VIDIOC_S_FMT");
        goto cleanup;
    }

    if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_RGB24) {
        fprintf(stderr, "RGB24 format not supported.\n");
        goto cleanup;
    }

    if (fmt.fmt.pix.width != CAMERA_WIDTH ||
        fmt.fmt.pix.height != CAMERA_HEIGHT) {

        printf("Warning: camera resolution is %dx%d\n",
               fmt.fmt.pix.width,
               fmt.fmt.pix.height);
    }

    // Request memory-mapped buffers
    CLEAR(req);

    req.count = 2;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("VIDIOC_REQBUFS");
        goto cleanup;
    }

    if (req.count == 0) {
        fprintf(stderr, "No camera buffers available.\n");
        goto cleanup;
    }

    buffers = calloc(req.count, sizeof(*buffers));

    if (!buffers) {
        perror("calloc");
        goto cleanup;
    }

    // Map camera buffers
    for (i = 0; i < req.count; i++) {

        CLEAR(buf);

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (xioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("VIDIOC_QUERYBUF");
            goto cleanup;
        }

        buffers[i].length = buf.length;

        buffers[i].start =
            v4l2_mmap(NULL,
                      buf.length,
                      PROT_READ | PROT_WRITE,
                      MAP_SHARED,
                      fd,
                      buf.m.offset);

        if (buffers[i].start == MAP_FAILED) {
            perror("v4l2_mmap");
            goto cleanup;
        }

        n_buffers++;
    }

    // Queue buffers
    for (i = 0; i < n_buffers; i++) {

        CLEAR(buf);

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF");
            goto cleanup;
        }
    }

    // Start camera streaming
    if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        perror("VIDIOC_STREAMON");
        goto cleanup;
    }

    streaming = true;

    printf("Camera streaming. Adjusting exposure...\n");

    // Discard initial frames to allow exposure adjustment
    for (i = 0; i < WARMUP_FRAMES;) {

        int result;

        do {
            FD_ZERO(&fds);
            FD_SET(fd, &fds);

            tv.tv_sec = 2;
            tv.tv_usec = 0;

            result = select(fd + 1,
                            &fds,
                            NULL,
                            NULL,
                            &tv);

        } while (result == -1 && errno == EINTR);

        if (result < 0) {
            perror("select");
            goto cleanup;
        }

        if (result == 0) {
            fprintf(stderr, "Camera timeout.\n");
            goto cleanup;
        }

        CLEAR(buf);

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        if (xioctl(fd, VIDIOC_DQBUF, &buf) < 0) {

            if (errno == EAGAIN) {
                continue;
            }

            perror("VIDIOC_DQBUF");
            goto cleanup;
        }

        if (buf.index >= n_buffers) {
            fprintf(stderr, "Invalid buffer index.\n");
            goto cleanup;
        }

        // Save only the final frame
        if (i == WARMUP_FRAMES - 1) {

            struct timespec timestamp;
            clock_gettime(CLOCK_REALTIME, &timestamp);

            snprintf(out_name,
                     sizeof(out_name),
                     "photo_%ld_%09ld.ppm",
                     (long)timestamp.tv_sec,
                     timestamp.tv_nsec);

            size_t rowBytes = fmt.fmt.pix.width * 3U;
            size_t stride = fmt.fmt.pix.bytesperline;

            if (stride < rowBytes) {
                stride = rowBytes;
            }

            size_t requiredBytes =
                (fmt.fmt.pix.height - 1U) * stride
                + rowBytes;

            if (buf.bytesused < requiredBytes ||
                buffers[buf.index].length < requiredBytes) {

                fprintf(stderr, "Incomplete camera frame.\n");
                goto cleanup;
            }

            fout = fopen(out_name, "wb");

            if (!fout) {
                perror("Cannot open image");
                goto cleanup;
            }

            bool saved = fprintf(fout,
                                 "P6\n%u %u\n255\n",
                                 fmt.fmt.pix.width,
                                 fmt.fmt.pix.height) >= 0;

            // Write RGB rows without any buffer padding
            uint8_t *image = buffers[buf.index].start;

            for (unsigned int row = 0;
                 saved && row < fmt.fmt.pix.height;
                 row++) {

                if (fwrite(image + row * stride,
                           1,
                           rowBytes,
                           fout) != rowBytes) {

                    saved = false;
                }
            }

            if (fclose(fout) != 0) {
                saved = false;
            }

            if (saved) {
                printf("Photo saved: %s\n", out_name);
            } else {
                fprintf(stderr, "Failed to save photo.\n");
                remove(out_name);
            }
        }

        // Return buffer to camera
        if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF");
            goto cleanup;
        }

        i++;
    }

cleanup:

    // Stop streaming
    if (streaming) {
        xioctl(fd, VIDIOC_STREAMOFF, &type);
    }

    // Release mapped buffers
    for (i = 0; i < n_buffers; i++) {

        v4l2_munmap(buffers[i].start,
                   buffers[i].length);
    }

    free(buffers);

    // Close camera
    if (fd >= 0) {
        v4l2_close(fd);
    }

    pthread_mutex_lock(&captureMutex);
    captureActive = false;
    pthread_mutex_unlock(&captureMutex);

    return NULL;
}

void Grabber_capture(void)
{
    pthread_t thread;

    pthread_mutex_lock(&captureMutex);

    // Prevent overlapping camera captures
    if (captureActive) {

        pthread_mutex_unlock(&captureMutex);

        printf("Camera is already capturing.\n");
        return;
    }

    captureActive = true;

    int result = pthread_create(&thread,
                                NULL,
                                grabber_thread,
                                NULL);

    if (result != 0) {

        captureActive = false;

        pthread_mutex_unlock(&captureMutex);

        fprintf(stderr,
                "Failed to create grabber thread: %s\n",
                strerror(result));

        return;
    }

    pthread_detach(thread);

    pthread_mutex_unlock(&captureMutex);
}

void Grabber_cleanup(void)
{
    // Wait for any active capture to finish
    while (1) {

        pthread_mutex_lock(&captureMutex);
        bool active = captureActive;
        pthread_mutex_unlock(&captureMutex);

        if (!active) {
            break;
        }

        struct timespec delay = {
            .tv_sec = 0,
            .tv_nsec = 10000000L
        };

        nanosleep(&delay, NULL);
    }
}