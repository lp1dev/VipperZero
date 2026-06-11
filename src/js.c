#include "js.h"
#include "serial.h"


static duk_ret_t js_serial_init(duk_context *ctx) {
    serial_init();
    return (duk_ret_t) 0;
}

static duk_ret_t js_serial_is_connected(duk_context *ctx) {
    int is_connected;
    
    is_connected = serial_is_connected();
    duk_push_int(ctx, is_connected);
    return (duk_ret_t) 1;
}

static duk_ret_t js_serial_close(duk_context *ctx) {
    serial_close();
    return (duk_ret_t) 0;
}

static duk_ret_t js_serial_available(duk_context *ctx) {
    unsigned int serial_bytes_available;

    serial_bytes_available = serial_available();
    duk_push_int(ctx, serial_bytes_available);
    return (duk_ret_t) 1;
}

static duk_ret_t js_serial_send(duk_context *ctx) {
    int sent;
    int length;
    char *data;

    data = (char *)duk_get_string(ctx, 0);
    sent = serial_send_line(data);
    duk_pop(ctx);
    duk_push_int(ctx, sent);
    return (duk_ret_t) 1;
}

static duk_ret_t js_serial_recv(duk_context *ctx) {
    // serial_recv(void *buf, unsigned int maxlen, int timeout_ms);
    char buffer[SERIAL_BUFFER_SIZE];
    int received;
    int timeout;

    timeout = duk_get_int(ctx, 0);
    received = serial_recv(buffer, SERIAL_BUFFER_SIZE, timeout);
    duk_pop(ctx);
    
    if (received > 0) {
        if (received < SERIAL_BUFFER_SIZE) {
            buffer[received] = '\0';
        }
        duk_push_string(ctx, buffer);
    }
    return (duk_ret_t) 1;
}

// int serial_send(const void *buf, unsigned int len);
// int serial_recv(void *buf, unsigned int maxlen, int timeout_ms);

/*

void add_duk_functions(void)

Adds vitter-specific C bindings to the JS context.

*/
void add_duk_functions(duk_context *ctx) {
    duk_push_c_function(ctx, js_serial_init, 0);
    duk_put_global_string(ctx, "serial_init");
    duk_push_c_function(ctx, js_serial_close, 0);
    duk_put_global_string(ctx, "serial_close");
    duk_push_c_function(ctx, js_serial_is_connected, 0);
    duk_put_global_string(ctx, "serial_is_connected");
    duk_push_c_function(ctx, js_serial_available, 0);
    duk_put_global_string(ctx, "serial_available");
    duk_push_c_function(ctx, js_serial_send, 1); // serial_send(str)
    duk_put_global_string(ctx, "serial_send");
    duk_push_c_function(ctx, js_serial_recv, 1); // serial_recv(timeout)
    duk_put_global_string(ctx, "serial_recv");
}