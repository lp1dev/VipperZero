#include "serial.h"
#include "../../src/net/tcp_debugger.h"

#include <taihen.h>
#include <stdio.h>
#include <string.h>
#include <psp2/usbserial.h>
#include <psp2/shellutil.h>
#include <psp2/kernel/threadmgr.h>

#define MAX_CHUNK 0x10000

static int g_initialized   = 0;
static int g_last_sce_err  = 0;

/* Debug: set while inside a send or recv. If a second I/O call enters while
 * this is non-zero, two threads are hitting the USB serial engine at once,
 * which is the most likely cause of the fast read/write crash. */
static volatile int g_io_active = 0;

static void dbg_hex(char *label, int value)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "0x%08x", value);
    debug(label, buf);
}

/* quark_debug variant of dbg_hex for the new instrumentation */
static void qdbg_hex(const char *label, unsigned int value)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "0x%08x", value);
    debug(label, buf);
}

int serial_last_sce_error(void)
{
    return g_last_sce_err;
}

int serial_init(void)
{
    if (g_initialized) {
        debug("serial_init: already initialized", NULL);
        return OK;
    }

    /* Wiki sample first checks status. 0x80244401 means "not started"
     * which is what we expect; anything else means something is already up. */
    int status_before = sceUsbSerialStatus();
    dbg_hex("status before start", status_before);
    //TODO handle if status is 0x80010058 (kernel plugin not installed) or < 0 (error)

    sceShellUtilLock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN);


    debug("serial_init: calling sceUsbSerialStart()", NULL);
    g_last_sce_err = sceUsbSerialStart();
    dbg_hex("sceUsbSerialStart ret", g_last_sce_err);

    if (g_last_sce_err != 0) {
        debug("serial_init: Start FAILED, aborting", NULL);
        /* Try to restore MTP so the Vita isn't stuck in no-USB land */
        sceUsbSerialClose();
        return ERR_START;
    }

    debug("serial_init: calling sceUsbSerialSetup(1)", NULL);
    g_last_sce_err = sceUsbSerialSetup(1);
    dbg_hex("sceUsbSerialSetup ret", g_last_sce_err);

    if (g_last_sce_err != 0) {
        debug("serial_init: Setup FAILED, closing", NULL);
        sceUsbSerialClose();
        return ERR_SETUP;
    }
    // Test from psp2waveviewer
    int res = 0;
    int retry = 3000;

    do {
		res = sceUsbSerialStatus();
		// sceClibPrintf("sceUsbSerialStatus 0x%X\n", res);
		sceKernelDelayThread(1000);
		retry--;
	} while(res == 0x80244401 && retry != 0); // not connected

	retry = 1000;

	do {
		res = sceUsbSerialStatus();
		sceKernelDelayThread(10000);
		retry--;
	} while(res == 0 && retry != 0);
    //
    dbg_hex("Serial status ", res);
    g_initialized = 1;
    debug("serial_init: SUCCESS", NULL);
    return OK;
}

int serial_close(void)
{
    if (!g_initialized) {
        debug("serial_close: not initialized, nothing to do", NULL);
        return OK;
    }
    debug("serial_close: calling sceUsbSerialClose()", NULL);
    sceUsbSerialClose();
    g_initialized = 0;
    debug("serial_close: done", NULL);
    return OK;
}

int serial_is_connected(void)
{
    if (!g_initialized) return 0;
    int s = sceUsbSerialStatus();
    dbg_hex("Serial status ", s);

    /* status returns 1 when the host has opened the port */
    return (s == 1) ? 1 : 0;
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

    qdbg_hex("serial_send ENTER tid", sceKernelGetThreadId());
    if (g_io_active)
        debug("serial_send: OVERLAP - another serial IO is already active", NULL);
    g_io_active = 1;

    dbg_hex("serial_send len", (int)len);
    int r = (int)sceUsbSerialSend(buf, len * sizeof(char), 0, 1000);
    dbg_hex("sceUsbSerialSend ret", r);
    g_last_sce_err = r;

    g_io_active = 0;
    qdbg_hex("serial_send EXIT tid", sceKernelGetThreadId());
    return r;
}

int serial_recv(void *buf, unsigned int maxlen, int timeout_ms)
{
    debug("serial_recv: ENTER", NULL);                 // ← first thing
    dbg_hex("serial_recv buf ptr", (int)(uintptr_t)buf);
    dbg_hex("serial_recv maxlen", maxlen);
    dbg_hex("serial_recv timeout", timeout_ms);

    if (!g_initialized) return ERR_NOT_OPEN;
    if (!buf || maxlen == 0) return ERR_INVALID;
    if (maxlen > MAX_CHUNK) maxlen = MAX_CHUNK;

    qdbg_hex("serial_recv ENTER tid", sceKernelGetThreadId());
    if (g_io_active)
        debug("serial_recv: OVERLAP - another serial IO is already active", NULL);
    g_io_active = 1;

    int slice_ms = 10;
    int waited = 0;

    while (waited < timeout_ms) {
        unsigned int avail = sceUsbSerialGetRecvBufferSize();
        dbg_hex("serial_recv avail", avail);            // ← per iteration
        if (avail > 0) {
            unsigned int take = avail < maxlen ? avail : maxlen;
            dbg_hex("serial_recv take", take);
            /* NOTE: -1 = block forever. If another thread drains the buffer
             * between the GetRecvBufferSize() above and this call, this wedges. */
            debug("serial_recv: calling sceUsbSerialRecv with infinite timeout", NULL);
            int r = (int)sceUsbSerialRecv(buf, take, 0, -1);
            dbg_hex("sceUsbSerialRecv ret", r);
            g_io_active = 0;
            qdbg_hex("serial_recv EXIT(data) tid", sceKernelGetThreadId());
            return r;
        }
        sceKernelDelayThread(slice_ms * 1000);
        waited += slice_ms;
    }
    debug("serial_recv: TIMEOUT", NULL);
    g_io_active = 0;
    qdbg_hex("serial_recv EXIT(timeout) tid", sceKernelGetThreadId());
    return 0;
}

int serial_send_line(const char *str)
{
    if (!str) return ERR_INVALID;
    qdbg_hex("serial_send_line ENTER tid", sceKernelGetThreadId());
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
    qdbg_hex("serial_recv_line ENTER tid", sceKernelGetThreadId());

    unsigned int used = 0;
    int waited = 0;
    int slice_ms = 50;

    while (used < maxlen - 1 && waited < timeout_ms) {
        char c;
        int r = serial_recv(&c, 1, slice_ms);
        if (r < 0) return r;
        if (r == 0) { waited += slice_ms; continue; }
        if (c == '\n') { buf[used] = '\0'; return (int)used; }
        if (c != '\r') buf[used++] = c;
        waited = 0;
    }
    buf[used] = '\0';
    debug("Received line", buf);
    return used > 0 ? (int)used : ERR_TIMEOUT;
}