#include "serial.h"

#include <string.h>
#include <psp2/usbserial.h>
#include <psp2/kernel/threadmgr.h>

#define MAX_CHUNK 0x10000

static int g_initialized = 0;

int serial_init(void)
{
    if (g_initialized) return OK;

    int ret = sceUsbSerialStart();
    if (ret < 0) return ERR_START;

    /* The 'unk' parameter to setup is consistently 1 in working homebrew. */
    ret = sceUsbSerialSetup(1);
    if (ret < 0) {
        sceUsbSerialClose();
        return ERR_SETUP;
    }

    g_initialized = 1;
    return OK;
}

int serial_close(void)
{
    if (!g_initialized) return OK;
    sceUsbSerialClose();
    g_initialized = 0;
    return OK;
}

int serial_is_connected(void)
{
    if (!g_initialized) return 0;
    return sceUsbSerialStatus();
}

unsigned int serial_available(void)
{
    if (!g_initialized) return 0;
    return sceUsbSerialGetRecvBufferSize();
}

int serial_send(const void *buf, unsigned int len)
{
    if (!g_initialized) return ERR_NOT_OPEN;
    if (!buf || len == 0)  return ERR_INVALID;
    if (len > MAX_CHUNK) len = MAX_CHUNK;

    return (int)sceUsbSerialSend(buf, len, 0, 0);
}

int serial_recv(void *buf, unsigned int maxlen, int timeout_ms)
{
    if (!g_initialized) return ERR_NOT_OPEN;
    if (!buf || maxlen == 0) return ERR_INVALID;
    if (maxlen > MAX_CHUNK) maxlen = MAX_CHUNK;

    /* Poll until data is available or we time out.
     * sceUsbSerialRecv on its own can block; checking the buffer size
     * lets us implement a clean timeout. */
    int slice_ms = 10;
    int waited = 0;

    while (waited < timeout_ms) {
        unsigned int avail = sceUsbSerialGetRecvBufferSize();
        if (avail > 0) {
            unsigned int take = avail < maxlen ? avail : maxlen;
            return (int)sceUsbSerialRecv(buf, take, 0, 0);
        }
        sceKernelDelayThread(slice_ms * 1000);
        waited += slice_ms;
    }

    return 0; /* timeout, no data */
}

int serial_send_line(const char *str)
{
    if (!str) return ERR_INVALID;

    size_t len = strlen(str);
    if (len > 0) {
        int r = serial_send(str, (unsigned int)len);
        if (r < 0) return r;
    }
    return serial_send("\n", 1);
}

int serial_recv_line(char *buf, unsigned int maxlen, int timeout_ms)
{
    if (!buf || maxlen < 2) return ERR_INVALID;

    unsigned int used  = 0;
    int          waited = 0;
    int          slice_ms = 50;

    while (used < maxlen - 1 && waited < timeout_ms) {
        char c;
        int r = serial_recv(&c, 1, slice_ms);
        if (r < 0) return r;
        if (r == 0) {
            waited += slice_ms;
            continue;
        }
        if (c == '\n') {
            buf[used] = '\0';
            return (int)used;
        }
        if (c != '\r') {
            buf[used++] = c;
        }
        /* Reset waited on activity */
        waited = 0;
    }

    buf[used] = '\0';
    return used > 0 ? (int)used : ERR_TIMEOUT;
}
