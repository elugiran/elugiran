#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <stdbool.h>

#include <linux/videodev2.h>

struct v4l2_frame_buffer
{
    void *addr;
    size_t len;
};

struct v4l2_buffer_pool
{
    struct v4l2_frame_buffer *buffer;
    size_t count;
};

static int open_device(const char *dev)
{
    int fd = open(dev, O_RDWR);
    if (-1 == fd)
        perror("open");

    return fd;
}

static void close_device(int fd)
{
    int ret = close(fd);
    if (-1 == ret)
        perror("close");
}

static int xioctl(int fd, unsigned long cmd, void *arg)
{
    int ret;
    do
    {
        ret = ioctl(fd, cmd, arg);
    } while (-1 == ret && EINTR == errno);

    if (-1 == ret)
        printf("cmd: %lx -> %s\n", cmd, strerror(errno));

    return ret;
}

static int check_cap(int fd)
{
    struct v4l2_capability cap = {0};
    int ret = xioctl(fd, VIDIOC_QUERYCAP, &cap);
    if (-1 == ret)
        return -1;

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE))
    {
        printf("is not capture device\n");
        return -1;
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING))
    {
        printf("is not streaming device\n");
        return -1;
    }

    return 0;
}

static int check_fmt(int fd)
{
    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    int ret = xioctl(fd, VIDIOC_G_FMT, &fmt);
    if (-1 == ret)
        return -1;

    printf("%dx%d : %d\n",
           fmt.fmt.pix.width,
           fmt.fmt.pix.height,
           fmt.fmt.pix.pixelformat);
    return 0;
}

static int request_buffer(int fd, unsigned int count)
{
    struct v4l2_requestbuffers req = {0};
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    req.count = count;

    int ret = xioctl(fd, VIDIOC_REQBUFS, &req);
    if (-1 == ret)
        return ret;
    else
        return req.count;
}

static struct v4l2_buffer_pool *query_buffer(int fd, unsigned int count)
{
    struct v4l2_buffer_pool *pool = malloc(sizeof(struct v4l2_buffer_pool));
    pool->count = count;
    pool->buffer = malloc(count * sizeof(struct v4l2_frame_buffer));

    for (size_t index = 0; index < count; ++index)
    {
        struct v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = index;

        xioctl(fd, VIDIOC_QUERYBUF, &buf);

        pool->buffer[index].len = buf.length;
        pool->buffer[index].addr = mmap(NULL,
                                        buf.length,
                                        PROT_READ, MAP_SHARED, fd, buf.m.offset);
        if (MAP_FAILED == pool->buffer[index].addr)
            printf("map failed at %ld\n", index);
    }

    return pool;
}

static int enqueu_buffer(int fd, int index)
{
    struct v4l2_buffer buf = {0};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = index;

    return xioctl(fd, VIDIOC_QBUF, &buf);
}

static int enqueue_pool(int fd, struct v4l2_buffer_pool *pool)
{
    for (size_t index = 0; index < pool->count; ++index)
    {
        int ret = enqueu_buffer(fd, index);
        if (-1 == ret)
            printf("enqueu erro at index: %ld\n", index);
    }
    return 0;
}

static void pool_cleanup(struct v4l2_buffer_pool *pool)
{
    for (size_t index = 0; index < pool->count; ++index)
        munmap(pool->buffer[index].addr, pool->buffer[index].len);
    free(pool->buffer);
    free(pool);
}

static int stream_control(int fd, bool status)
{
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    unsigned long cmd = (status) ? VIDIOC_STREAMON : VIDIOC_STREAMOFF;
    return xioctl(fd, cmd, &type);
}

static int dequeue_buffer(int fd)
{
    struct v4l2_buffer buf = {0};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    int ret = xioctl(fd, VIDIOC_DQBUF, &buf);
    if (-1 == ret)
        return -1;
    else
        return buf.index;
}

static void write_frame(const struct v4l2_frame_buffer *buffer)
{
    int fd = open("frame.raw", O_RDWR | O_CREAT | O_TRUNC, 0644);
    write(fd, buffer->addr, buffer->len);
    fsync(fd);
    close(fd);
}

int main(int argc, char *argv[])
{
    const char *dev = (2 == argc) ? argv[1] : "/dev/video0";

    int fd = open_device(dev);
    int ret = check_cap(fd);
    if (-1 == ret)
        return EXIT_FAILURE;
    check_fmt(fd);

    int count = request_buffer(fd, 1000);
    struct v4l2_buffer_pool *pool = query_buffer(fd, count);
    enqueue_pool(fd, pool);
    stream_control(fd, true);

    for (size_t index = 0; index < 10; ++index)
    {
        int tmp = dequeue_buffer(fd);
        enqueu_buffer(fd, tmp);
    }

    int index = dequeue_buffer(fd);
    write_frame(&pool->buffer[index]);
    enqueu_buffer(fd, index);

    stream_control(fd, false);
    pool_cleanup(pool);
    close_device(fd);

    return 0;
}
