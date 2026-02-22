#ifndef WS_H
#define WS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "types.h"


#define WS_OK      0
#define WS_AGAIN   1
#define WS_CLOSED -1
#define WS_ERROR  -2

#include <ctype.h>
#include <string.h>





typedef enum {
    WS_RECV_HDR,
    WS_RECV_EXT_LEN,
    WS_RECV_MASK,
    WS_RECV_PAYLOAD
} ws_recv_phase_t;


typedef struct {
    char req[1024];
    size_t req_len;
    char resp[4096];
    size_t woff;
    size_t roff;
    char key[25];
    bool key_gend;
    int initialized;
} ws_handshake_t;

typedef struct {
    ws_recv_phase_t phase;
    uint8_t hdr[2];
    size_t hdr_off;
    bool fragmented;
    uint8_t ext[8];
    size_t ext_len;
    size_t ext_off;
    uint8_t mask[4];
    size_t mask_off;
    uint8_t opcode;
    uint64_t payload_len;
    int masked;
    uint8_t *payload;
    size_t payload_off;
    bool message_in_progress;
    uint8_t message_opcode;
    uint8_t *message_buf;
    size_t message_len;
    size_t message_cap;
    bool fin;
} ws_recv_ctx_t;

int tcp_read(void *ctx, void *buf, size_t len, size_t *out);
int tls_read(void *ctx, void *buf, size_t len, size_t *out);
int tcp_write(void *ctx, const void *buf, size_t len);
int tls_write(void *ctx, const void *buf, size_t len);

ws_hs_rc_t ws_handshake(ws_io_t *io, const char *host, const char *path, ws_handshake_t *hs);
int ws_send_text(ws_io_t *io, const char* msg);
int ws_recv_step(ws_io_t *io, ws_recv_ctx_t *ctx, uint8_t **out, size_t *out_len, ws_thread_ctx_t *flags);
void ws_reset(ws_recv_ctx_t *ctx);
void* websocket_thread(void *ctx);

#endif
