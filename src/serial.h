/*
 * serial.h - minimal blocking serial port API.
 *
 * Default device: "/dev/ttyUSB0", overridable at runtime via the
 * EVILCROW_SERIAL_PORT environment variable, or at compile time with
 *   -DSERIAL_DEVICE='"/dev/ttyACM0"'  -DSERIAL_BAUD=B115200
 */

#ifndef SERIAL_H
#define SERIAL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Suggested buffer size for callers reading lines / responses. Override by
 * defining it before including this header if you need a different size. */
#ifndef SERIAL_BUFFER_SIZE
#define SERIAL_BUFFER_SIZE 4096
#endif

int serial_init(void);
int serial_close(void);

/* Returns 1 if the host has opened the port, 0 otherwise. */
int serial_is_connected(void);

/* Bytes currently buffered for reading. */
unsigned int serial_available(void);

/* Raw I/O. Returns bytes sent/received, or negative on error.
 * Max length per call is 0x10000. */
int serial_send(const void *buf, unsigned int len);
int serial_recv(void *buf, unsigned int maxlen, int timeout_ms);

/* Line-oriented helpers (text protocol). */
int serial_send_line(const char *str);
int serial_recv_line(char *buf, unsigned int maxlen, int timeout_ms);

/* Discard all pending input (kernel queue + internal buffer). Call before
 * sending a command whose reply you intend to read, to avoid reading
 * leftover bytes from a previous response / boot banner / async output. */
void serial_flush_input(void);

/* Send a command and collect its full reply, resync-safe.
 *
 * Flushes input, sends "<cmd>\n", then reads lines until one starts with
 * "OK" or "ERR" (the firmware terminators), accumulating them into `reply`
 * (newline-separated, NUL-terminated, terminator line included).
 *
 *   overall_timeout_ms : upper bound on the whole exchange (-1 = no bound)
 *   line_idle_ms       : per-line idle allowance; a quiet gap this long
 *                        after some reply has arrived ends the read. Use a
 *                        generous value (e.g. 1500) for slow / low-power
 *                        peers that trickle bytes.
 *
 * Returns reply length (>=0), -1 on I/O error, -2 on timeout. */
int serial_command(const char *cmd, char *reply, unsigned int reply_cap,
                   int overall_timeout_ms, int line_idle_ms);

#ifdef __cplusplus
}
#endif

#endif /* SERIAL_H */