/*
 * serial.c - POSIX implementation of the serial.h API.
 *
 * Adds an internal RX buffer so:
 *   - byte-by-byte reads in serial_recv_line don't syscall per byte
 *   - serial_available is reliable on usb-serial drivers (FIONREAD lies)
 *   - oversized lines don't leave half a line stuck in the kernel buffer
 *
 * Build:  cc -O2 -c serial.c
 * Link:   cc -O2 your_program.c serial.c -o your_program
 */

#include "serial.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#ifndef SERIAL_DEVICE
#define SERIAL_DEVICE "/dev/ttyUSB0"
#endif

#ifndef SERIAL_BAUD
#define SERIAL_BAUD B115200
#endif

#define SERIAL_MAX_LEN  0x10000
#define RX_BUF_SIZE     4096

static int           s_fd = -1;

/* Internal RX ring. Linear with compaction (simpler than a true ring;
 * the buffer is small and memmove is cheap). */
static unsigned char s_rx[RX_BUF_SIZE];
static unsigned int  s_rx_head = 0;   /* one-past last byte written */
static unsigned int  s_rx_tail = 0;   /* next byte to read */

static unsigned int rx_buffered(void) { return s_rx_head - s_rx_tail; }

static void rx_compact(void) {
    if (s_rx_tail == 0) return;
    unsigned int n = rx_buffered();
    if (n > 0) memmove(s_rx, s_rx + s_rx_tail, n);
    s_rx_head = n;
    s_rx_tail = 0;
}

/* Refill the internal buffer from the port.
 * Returns bytes added, 0 on timeout, -1 on error. */
static int rx_refill(int timeout_ms) {
    if (s_fd < 0) return -1;
    rx_compact();
    if (s_rx_head >= RX_BUF_SIZE) return 0;     /* buffer full - caller must drain */

    struct pollfd pfd = { .fd = s_fd, .events = POLLIN };
    int pr;
    do { pr = poll(&pfd, 1, timeout_ms); } while (pr < 0 && errno == EINTR);
    if (pr < 0) return -1;
    if (pr == 0) return 0;
    if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) return -1;

    ssize_t n = read(s_fd, s_rx + s_rx_head, RX_BUF_SIZE - s_rx_head);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
        return -1;
    }
    if (n == 0) return 0;
    s_rx_head += (unsigned int)n;
    return (int)n;
}

int serial_init(void) {
    if (s_fd >= 0) return 0;
    s_rx_head = s_rx_tail = 0;

    const char *path = getenv("EVILCROW_SERIAL_PORT");
    if (!path || !*path) path = SERIAL_DEVICE;

    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return -1;

    struct termios tio;
    if (tcgetattr(fd, &tio) < 0) { close(fd); return -1; }

    cfmakeraw(&tio);
    cfsetispeed(&tio, SERIAL_BAUD);
    cfsetospeed(&tio, SERIAL_BAUD);
    tio.c_cflag |= CLOCAL | CREAD;
    tio.c_cflag &= ~CRTSCTS;
    tio.c_iflag &= ~(IXON | IXOFF | IXANY);
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tio) < 0) { close(fd); return -1; }
    tcflush(fd, TCIOFLUSH);

    s_fd = fd;
    return 0;
}

int serial_close(void) {
    if (s_fd < 0) return 0;
    int r = close(s_fd);
    s_fd = -1;
    s_rx_head = s_rx_tail = 0;
    return (r < 0) ? -1 : 0;
}

int serial_is_connected(void) {
    if (s_fd < 0) return 0;
    struct termios tio;
    if (tcgetattr(s_fd, &tio) < 0) {
        close(s_fd);
        s_fd = -1;
        s_rx_head = s_rx_tail = 0;
        return 0;
    }
    return 1;
}

unsigned int serial_available(void) {
    if (s_fd < 0) return 0;
    /* Opportunistic non-blocking refill so the count reflects what's
     * actually readable, not just what FIONREAD happens to report. */
    rx_refill(0);
    return rx_buffered();
}

int serial_send(const void *buf, unsigned int len) {
    if (s_fd < 0)             return -1;
    if (len == 0)             return 0;
    if (len > SERIAL_MAX_LEN) return -1;

    const unsigned char *p = (const unsigned char *)buf;
    unsigned int remaining = len;
    while (remaining > 0) {
        ssize_t w = write(s_fd, p, remaining);
        if (w < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd pfd = { .fd = s_fd, .events = POLLOUT };
                if (poll(&pfd, 1, 1000) <= 0) return -1;
                continue;
            }
            return -1;
        }
        p += w;
        remaining -= (unsigned int)w;
    }
    return (int)len;
}

int serial_recv(void *buf, unsigned int maxlen, int timeout_ms) {
    if (s_fd < 0)             return -1;
    if (maxlen == 0)          return 0;
    if (maxlen > SERIAL_MAX_LEN) maxlen = SERIAL_MAX_LEN;

    if (rx_buffered() == 0) {
        int r = rx_refill(timeout_ms);
        if (r < 0) return -1;
        if (rx_buffered() == 0) return 0;
    }

    unsigned int avail = rx_buffered();
    unsigned int n = avail < maxlen ? avail : maxlen;
    memcpy(buf, s_rx + s_rx_tail, n);
    s_rx_tail += n;
    return (int)n;
}

int serial_send_line(const char *str) {
    if (s_fd < 0 || !str) return -1;
    unsigned int len = (unsigned int)strlen(str);
    if (len > SERIAL_MAX_LEN - 1) return -1;
    if (len > 0 && serial_send(str, len) < 0) return -1;
    if (serial_send("\n", 1) < 0) return -1;
    return (int)(len + 1);
}

/* Read until '\n', the buffer fills, or timeout_ms elapses overall.
 * '\r' is dropped. The result is always NUL-terminated when maxlen >= 1.
 *
 * Returns:
 *    >0  : line of N chars received (NUL written at buf[N])
 *     0  : empty line received ('\n' with nothing before it)
 *    -1  : I/O error
 *    -2  : timed out without seeing '\n'
 *
 * Crucially: if the line is longer than maxlen-1, we keep consuming bytes
 * from the port until we see '\n', discarding the overflow. That way the
 * next call always starts at a fresh line - no half-lines stuck behind. */
int serial_recv_line(char *buf, unsigned int maxlen, int timeout_ms) {
    if (s_fd < 0 || !buf || maxlen == 0) return -1;

    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    unsigned int n = 0;
    int          truncating = 0;
    int          timed_out  = 0;

    for (;;) {
        /* Drain whatever's already buffered. */
        while (rx_buffered() > 0) {
            unsigned char c = s_rx[s_rx_tail++];
            if (c == '\r') continue;
            if (c == '\n') { buf[n] = 0; return (int)n; }
            if (truncating) continue;
            if (n + 1 < maxlen) {
                buf[n++] = (char)c;
            } else {
                truncating = 1;            /* keep going until '\n' */
            }
        }

        /* Need more bytes - refill within the remaining time budget. */
        int remaining = timeout_ms;
        if (timeout_ms >= 0) {
            struct timespec t1;
            clock_gettime(CLOCK_MONOTONIC, &t1);
            long elapsed_ms = (long)(t1.tv_sec  - t0.tv_sec ) * 1000
                            + (long)(t1.tv_nsec - t0.tv_nsec) / 1000000;
            remaining = timeout_ms - (int)elapsed_ms;
            if (remaining <= 0) { timed_out = 1; break; }
        }
        int r = rx_refill(remaining);
        if (r < 0)  { buf[n] = 0; return -1; }
        if (r == 0) { timed_out = 1; break; }
    }

    buf[n] = 0;
    return timed_out ? -2 : (int)n;
}