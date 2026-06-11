#ifndef SERIAL_H_
#define SERIAL_H_

#include <stddef.h>

/* USB serial to a host (e.g. Raspberry Pi acting as USB host).
 *
 * Uses SceUsbSerial to make the Vita appear as a CDC-ACM serial device.
 * On Linux, the host will see /dev/ttyACM0 (or similar).
 *
 * Important: this uses UDCD. The hidkeyboard plugin must NOT be active
 * while pi_serial is running. Call hidkeyboard_user_stop() (or skip
 * keyboard_init() entirely) before calling pi_serial_init(). */

#define OK              0
#define ERR_START      -1
#define ERR_SETUP      -2
#define ERR_NOT_OPEN   -3
#define ERR_INVALID    -4
#define ERR_TIMEOUT    -5
#define SERIAL_BUFFER_SIZE 2048

/* Lifecycle */
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

#endif /* SERIAL_H */
