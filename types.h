#ifndef TYPES_H
#define TYPES_H

#include <stdbool.h>
#include <stdatomic.h>

typedef enum {
    WS_HS_OK = 0,
    WS_HS_AGAIN,
    WS_HS_ERROR
} ws_hs_rc_t;


typedef struct {
    atomic_bool running;
    int sock;
    _Atomic(char *) json_inbound;
    atomic_bool json_inbound_exists;
    _Atomic(char *) json_outbound;
    atomic_bool json_outbound_exists;

    _Atomic(char*) console_outbound;
    atomic_bool console_outbound_exists;

    atomic_int close_code;
} ws_thread_flags_t;
#endif
