#ifndef TYPES_H
#define TYPES_H

#include <stdbool.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stddef.h>
#include <openssl/ssl.h>
typedef enum {
    WS_HS_OK = 0,
    WS_HS_AGAIN,
    WS_HS_ERROR
} ws_hs_rc_t;

typedef struct {
    int sock;
    bool encrypted;
    int (*read)(void *ctx, void *buf, size_t len, size_t *out);
    int (*write)(void *ctx, const void *buf, size_t len);
    void *ctx;
} ws_io_t;



typedef struct {
    char **message;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    pthread_mutex_t lock;
} ws_queue_t;

typedef struct {
    atomic_bool running;
    int sock;
    SSL *ssl;
    ws_io_t io;

    ws_queue_t json_inbound;
    ws_queue_t json_outbound;
    ws_queue_t console_outbound;

    atomic_int close_code;

    uint8_t *out_frame;
    size_t out_len;
    size_t out_sent;
    bool want_write;

} ws_thread_ctx_t;
#endif
