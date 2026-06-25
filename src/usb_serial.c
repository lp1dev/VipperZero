/*
 * serial.c - POSIX implementation of the serial.h API.
 *
 * Internal RX buffering so:
 *   - byte-by-byte reads don't syscall per byte
 *   - serial_available is reliable on usb-serial drivers (FIONREAD lies)
 *   - oversized lines don't leave half a line stuck in the kernel buffer
 *
 * Robustness additions (this revision):
 *   - serial_flush_input(): discard stale RX (kernel + internal buffer)
 *   - serial_command(): flush, send line, read the full reply up to an
 *     OK/ERR terminator, resync-safe. This is the fix for "garbage" and
 *     "empty" responses, which were caused by reading leftover bytes from
 *     a previous command / boot banner / async NOTIF lines.
 *   - inter-byte idle timeout in line reads so a slow, trickle-fed response
 *     from a low-power peer isn't cut off mid-line.
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

/* Internal RX buffer. Linear with compaction. */
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

/* Milliseconds elapsed since *t0 on the monotonic clock. */
static long ms_since(const struct timespec *t0) {
    struct timespec t1;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    return (long)(t1.tv_sec  - t0->tv_sec ) * 1000
         + (long)(t1.tv_nsec - t0->tv_nsec) / 1000000;
}

/* Refill the internal buffer from the port.
 * Returns bytes added, 0 on timeout, -1 on hard error. */
static int rx_refill(int timeout_ms) {
    if (s_fd < 0) return -1;
    rx_compact();
    if (s_rx_head >= RX_BUF_SIZE) return 0;     /* full - caller must drain */

    struct pollfd pfd = { .fd = s_fd, .events = POLLIN, .revents = 0 };
    int pr;
    do { pr = poll(&pfd, 1, timeout_ms); } while (pr < 0 && errno == EINTR);
    if (pr < 0) return -1;
    if (pr == 0) return 0;

    /* If the device vanished (unplug), POLLHUP/ERR fire. But POLLIN may be
     * set at the same time with the last buffered bytes - read those first,
     * and only report error if there was genuinely nothing to read. */
    if (pfd.revents & POLLIN) {
        ssize_t n = read(s_fd, s_rx + s_rx_head, RX_BUF_SIZE - s_rx_head);
        if (n > 0) { s_rx_head += (unsigned int)n; return (int)n; }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            return 0;
        return -1;
    }
    if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) return -1;
    return 0;
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
    /* Drop framing/parity-error bytes rather than passing through 0x00 /
     * 0xFF garbage when the line glitches (common with weak USB power). */
    tio.c_iflag &= ~(INPCK | ISTRIP | IGNBRK | BRKINT | PARMRK);
    tio.c_iflag |= IGNPAR;
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tio) < 0) { close(fd); return -1; }

    s_fd = fd;

    /* The ESP32 resets on open (CP2102N DTR/RTS toggle). Give it time to
     * boot, then discard the boot banner so the first command's response
     * isn't polluted by it. */
    usleep(1600 * 1000);
    {
        struct timespec t0; clock_gettime(CLOCK_MONOTONIC, &t0);
        unsigned char tmp[256];
        for (;;) {
            struct pollfd pfd = { .fd = s_fd, .events = POLLIN, .revents = 0 };
            if (poll(&pfd, 1, 80) <= 0) break;          /* 80ms idle = done */
            if (read(s_fd, tmp, sizeof(tmp)) <= 0) break;
            if (ms_since(&t0) > 3000) break;            /* hard cap */
        }
    }
    tcflush(s_fd, TCIOFLUSH);
    s_rx_head = s_rx_tail = 0;
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
    rx_refill(0);
    return rx_buffered();
}

/* Discard everything pending: kernel RX queue + our internal buffer.
 * Call this before sending a command whose reply you intend to read. */
void serial_flush_input(void) {
    if (s_fd < 0) return;
    tcflush(s_fd, TCIFLUSH);
    /* tcflush doesn't catch bytes already pulled into the usb-serial driver
     * stage, so also drain non-blocking until quiet. */
    unsigned char tmp[256];
    for (;;) {
        struct pollfd pfd = { .fd = s_fd, .events = POLLIN, .revents = 0 };
        if (poll(&pfd, 1, 0) <= 0) break;
        if (read(s_fd, tmp, sizeof(tmp)) <= 0) break;
    }
    s_rx_head = s_rx_tail = 0;
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
                struct pollfd pfd = { .fd = s_fd, .events = POLLOUT, .revents = 0 };
                if (poll(&pfd, 1, 1000) <= 0) return -1;
                continue;
            }
            return -1;
        }
        p += w;
        remaining -= (unsigned int)w;
    }
    /* Ensure the bytes are actually pushed to the device before we start
     * waiting for a reply (avoids a race where we time out before the
     * command has even left the host). */
    tcdrain(s_fd);
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
    /* Build "<str>\n" and send in one write so the command line can't be
     * split across two USB packets with a gap the firmware might mis-time. */
    char stackbuf[512];
    char *tmp = stackbuf;
    if (len + 1 > sizeof(stackbuf)) {
        tmp = (char *)malloc(len + 1);
        if (!tmp) return -1;
    }
    memcpy(tmp, str, len);
    tmp[len] = '\n';
    int rc = serial_send(tmp, len + 1);
    if (tmp != stackbuf) free(tmp);
    return (rc < 0) ? -1 : (int)(len + 1);
}

/* Read until '\n', the buffer fills, or timeout_ms elapses overall.
 * '\r' is dropped. Always NUL-terminated when maxlen >= 1.
 *
 * Returns:
 *    >0  : line of N chars received
 *     0  : empty line ('\n' alone)
 *    -1  : I/O error
 *    -2  : timed out without seeing '\n'
 *
 * Oversized lines: keep consuming until '\n', discarding overflow, so the
 * next call always starts clean. */
int serial_recv_line(char *buf, unsigned int maxlen, int timeout_ms) {
    if (s_fd < 0 || !buf || maxlen == 0) return -1;

    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    unsigned int n = 0;
    int          truncating = 0;

    for (;;) {
        while (rx_buffered() > 0) {
            unsigned char c = s_rx[s_rx_tail++];
            if (c == '\r') continue;
            if (c == '\n') { buf[n] = 0; return (int)n; }
            /* Drop non-printable noise bytes that aren't part of the text
             * protocol (NUL, 0xFF and friends from line glitches). Tab is
             * kept; everything else below space except \n/\r is dropped. */
            if (c != '\t' && (c < 0x20 || c == 0x7f)) continue;
            if (truncating) continue;
            if (n + 1 < maxlen) buf[n++] = (char)c;
            else truncating = 1;
        }

        int remaining = timeout_ms;
        if (timeout_ms >= 0) {
            remaining = timeout_ms - (int)ms_since(&t0);
            if (remaining <= 0) { buf[n] = 0; return -2; }
        }
        int r = rx_refill(remaining);
        if (r < 0)  { buf[n] = 0; return -1; }
        if (r == 0) { buf[n] = 0; return -2; }
    }
}

/* Send a command and collect its full reply, resync-safe.
 *
 * Flushes stale input first, sends "<cmd>\n", then reads lines until one
 * begins with "OK" or "ERR" (the firmware's terminators), appending each to
 * `reply` (newline-separated). `overall_timeout_ms` bounds the whole wait;
 * `line_idle_ms` is the per-line idle allowance for slow trickle responses.
 *
 * Returns total reply length on success (>=0), -1 on I/O error, -2 on
 * timeout before a terminator arrived. The terminating OK/ERR line IS
 * included in the reply so callers can inspect success/failure. */
int serial_command(const char *cmd, char *reply, unsigned int reply_cap,
                   int overall_timeout_ms, int line_idle_ms) {
    if (s_fd < 0 || !reply || reply_cap == 0) return -1;

    serial_flush_input();
    if (cmd && serial_send_line(cmd) < 0) return -1;

    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    unsigned int w = 0;
    char line[600];
    reply[0] = 0;

    for (;;) {
        int budget = line_idle_ms;
        if (overall_timeout_ms >= 0) {
            int left = overall_timeout_ms - (int)ms_since(&t0);
            if (left <= 0) return -2;
            if (budget < 0 || budget > left) budget = left;
        }

        int n = serial_recv_line(line, sizeof(line), budget);
        if (n == -1) return -1;
        if (n == -2) {
            /* No line within this budget. If we've already collected some
             * reply, treat a quiet gap as end-of-response; else keep waiting
             * until the overall timeout. */
            if (w > 0) return (int)w;
            if (overall_timeout_ms >= 0 && ms_since(&t0) >= overall_timeout_ms)
                return -2;
            continue;
        }

        /* Append line + '\n' to reply if it fits. */
        unsigned int ln = (unsigned int)n;
        if (w + ln + 1 < reply_cap) {
            memcpy(reply + w, line, ln);
            w += ln;
            reply[w++] = '\n';
            reply[w] = 0;
        }

        if (!strncmp(line, "OK", 2) || !strncmp(line, "ERR", 3))
            return (int)w;
    }
}