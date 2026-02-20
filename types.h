#ifndef TYPES_H
#define TYPES_H

#include <stdbool.h>
#include <stdatomic.h>
#include <pthread.h>

typedef enum {
    WS_HS_OK = 0,
    WS_HS_AGAIN,
    WS_HS_ERROR
} ws_hs_rc_t;

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

    ws_queue_t json_inbound;
    ws_queue_t json_outbound;
    ws_queue_t console_outbound;

    atomic_int close_code;
} ws_thread_ctx_t;
#endif
