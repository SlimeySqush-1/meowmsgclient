#include "ws.h"
#include "utils.h"
#include <openssl/ssl3.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/random.h>
#include <stdatomic.h>
#include <openssl/sha.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <openssl/ssl.h>

#define MAX_EVENTS 4
#define MAX_WS_PAYLOAD 16777216
#define WS_OPCODE_CONTINUATION 0x0
#define WS_OPCODE_TEXT 0x1
#define WS_OPCODE_BINARY 0x2
#define WS_OPCODE_CLOSE 0x8
#define WS_OPCODE_PING 0x9
#define WS_OPCODE_PONG 0xA

enum {
   WS_RECV_OK = 0,      // full message ready
   WS_RECV_AGAIN = 1,   // need more data
   WS_RECV_CLOSED = -1,
   WS_RECV_ERROR = -2
};

#define WS_OK 0
#define WS_AGAIN 1
#define WS_CLOSED -1
#define WS_ERROR -2
#define WS_NORMAL_CLOSE 1000
#define WS_PAYLOAD_TOO_BIG 1009

int tcp_read(void *ctx, void *buf, size_t len, size_t *out) {
    int sock = *(int*)ctx;

    ssize_t n = recv(sock, buf, len, 0);
    if (n > 0) {
        *out = (size_t)n;
        return WS_OK;
    }

    if (n == 0)
        return WS_CLOSED;

    if (errno == EAGAIN || errno == EWOULDBLOCK)
        return WS_AGAIN;

    return WS_ERROR;
}

int tcp_write(void *ctx, const void *buf, size_t len) {
    int sock = *(int*)ctx;

    ssize_t n = send(sock, buf, len, 0);
    if (n >= 0)
        return (int)n;

    if (errno == EAGAIN || errno == EWOULDBLOCK)
        return WS_AGAIN;

    return WS_ERROR;
}

int tls_read(void *ctx, void *buf, size_t len, size_t *out) {
    SSL *ssl = (SSL*)ctx;

    int n = SSL_read(ssl, buf, len);
    if (n > 0) {
        *out = (size_t)n;
        return WS_OK;
    }

    int err = SSL_get_error(ssl, n);

    if (err == SSL_ERROR_WANT_READ ||
        err == SSL_ERROR_WANT_WRITE)
        return WS_AGAIN;

    if (err == SSL_ERROR_ZERO_RETURN)
        return WS_CLOSED;

    return WS_ERROR;
}

int tls_write(void *ctx, const void *buf, size_t len) {
    SSL *ssl = (SSL*)ctx;

    int n = SSL_write(ssl, buf, len);
    if (n > 0)
        return n;

    int err = SSL_get_error(ssl, n);

    if (err == SSL_ERROR_WANT_READ ||
        err == SSL_ERROR_WANT_WRITE)
        return WS_AGAIN;

    return WS_ERROR;
}

uint8_t *build_frame(const char *msg, size_t *out_len)
{
    size_t payload_len = strlen(msg);

    size_t header_len = 2;

    if (payload_len >= 126 && payload_len <= 65535) {
        header_len += 2;
    } else if (payload_len > 65535) {
        header_len += 8;
    }

    size_t total_len = header_len + payload_len;

    uint8_t *frame = malloc(total_len);
    if (!frame)
        return NULL;

    size_t offset = 0;

    frame[offset++] = 0x81;

    if (payload_len < 126) {
        frame[offset++] = (uint8_t)payload_len;

    } else if (payload_len <= 65535) {
        frame[offset++] = 126;
        frame[offset++] = (payload_len >> 8) & 0xFF;
        frame[offset++] = payload_len & 0xFF;

    } else {
        frame[offset++] = 127;

        for (int i = 7; i >= 0; i--) {
            frame[offset++] = (payload_len >> (i * 8)) & 0xFF;
        }
    }

    memcpy(frame + offset, msg, payload_len);

    *out_len = total_len;

    return frame;
}

int ascii_strncasecmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];

        if (cb == '\0')
            return 0;

        if (tolower(ca) != tolower(cb))
            return tolower(ca) - tolower(cb);
    }
    return 0;
}

char *ascii_strcasestr(const char *haystack, const char *needle) {
    if (!*needle)
        return (char *)haystack;

    size_t needle_len = strlen(needle);

    for (; *haystack; haystack++) {
        if (ascii_strncasecmp(haystack, needle, needle_len) == 0)
            return (char *)haystack;
    }

    return NULL;
}

static int append_to_message(ws_recv_ctx_t *ctx,
                             uint8_t *data,
                             size_t len) {
    size_t needed = ctx->message_len + len;

    if (needed > MAX_WS_PAYLOAD)
        return WS_PAYLOAD_TOO_BIG;

    if (needed > ctx->message_cap) {
        size_t new_cap = needed * 2;
        uint8_t *tmp = realloc(ctx->message_buf, new_cap);
        if (!tmp) return WS_ERROR;
        ctx->message_buf = tmp;

        ctx->message_cap = new_cap;
    }

    memcpy(ctx->message_buf + ctx->message_len, data, len);
    ctx->message_len += len;
    return WS_OK;
}
static int init_message_buffer(ws_recv_ctx_t *ctx,
                               uint8_t *data,
                               size_t len) {
    if (len > MAX_WS_PAYLOAD)
        return WS_PAYLOAD_TOO_BIG;

    ctx->message_buf = malloc(len ? len : 1);
    if (!ctx->message_buf)
        return WS_ERROR;

    memcpy(ctx->message_buf, data, len);

    ctx->message_len = len;
    ctx->message_cap = len;
    ctx->message_in_progress = true;

    return WS_OK;
}

static void ws_clear_message(ws_recv_ctx_t *ctx) {
    if (ctx->message_buf) {
        free(ctx->message_buf);
        ctx->message_buf = NULL;
    }
    ctx->message_len = 0;
    ctx->message_cap = 0;
    ctx->message_in_progress = false;
}

void ws_reset(ws_recv_ctx_t *ctx) {

    ctx->phase = WS_RECV_HDR;
    ctx->hdr_off = 0;
    ctx->fragmented = false;
    ctx->ext_off = 0;
    ctx->mask_off = 0;
    ctx->payload_off = 0;
    ctx->payload_len = 0;
    ctx->opcode = 0;
    ctx->masked = 0;

    ctx->payload = NULL;
}

void ws_quit(ws_recv_ctx_t *ctx) {
    if (ctx->payload) {
        free(ctx->payload);
        ctx->payload = NULL;
    }
    ws_reset(ctx);
}


static void mask_payload(unsigned char *data, size_t len, unsigned char mask[4]) {
    for (size_t i = 0; i < len; i++)
        data[i] ^= mask[i % 4];
}

ws_hs_rc_t ws_handshake(ws_io_t *io, const char *host, const char *path, ws_handshake_t *hs) {
    if (!hs) {
        perror("ws_handshake: NULL handshake");
        return WS_HS_ERROR;
    }
    if (hs->initialized) {
        perror("ws_handshake: handshake already initialized");
        return WS_HS_OK;
    }

    unsigned char raw_key[16];
    if (!hs->key_gend) {

        if (getrandom(raw_key, sizeof(raw_key), 0) != sizeof(raw_key)) {
            perror("getrandom");
        return WS_HS_ERROR;
        }
        base64_encode(raw_key, 16, hs->key);

        hs->key_gend = true;



    hs->req_len = snprintf(hs->req, sizeof(hs->req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n",
        path, host, hs->key);
    }
    if (hs->req_len >= sizeof(hs->req))
        return WS_HS_ERROR;

    // Write request to socket
    while (hs->woff < hs->req_len) {
        int n = io->write(io->ctx, hs->req + hs->woff, hs->req_len - hs->woff);

        if (n == WS_AGAIN)
            return WS_HS_AGAIN;

        if (n <= 0)
            return WS_HS_ERROR;

        hs->woff += n;
    }

    // Read response from socket
    while (hs->roff + 1 < sizeof(hs->resp)) {
        size_t n;
        int rc = io->read(io->ctx, hs->resp + hs->roff, sizeof(hs->resp) - hs->roff - 1, &n);
        if (rc == WS_AGAIN) return WS_HS_AGAIN;
        if (rc != WS_OK) return WS_HS_ERROR;

        hs->roff += n;
        hs->resp[hs->roff] = 0;
        if (strstr(hs->resp, "\r\n\r\n")) break;
    }
    if (!strstr(hs->resp, "\r\n\r\n"))
        return WS_HS_ERROR;

    unsigned char sha1_out[20];
    char concatenated[128];
    char expected_key[64];

    snprintf(concatenated, sizeof(concatenated),
             "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11",
             hs->key);

    SHA1((unsigned char*)concatenated,
         strlen(concatenated),
         sha1_out);

    base64_encode(sha1_out, 20, expected_key);
    char *accept = ascii_strcasestr(hs->resp, "sec-websocket-accept:");
    if (!accept) return WS_HS_ERROR;

    accept += strlen("sec-websocket-accept:");
    while (*accept == ' ') accept++;

    char *end = strstr(accept, "\r\n");
    if (!end) return WS_HS_ERROR;

    *end = '\0';

    if (strncmp(hs->resp, "HTTP/1.1 101", 12) != 0)
        return WS_HS_ERROR;

    char *upgrade = ascii_strcasestr(hs->resp, "upgrade:");
    if (!upgrade || !ascii_strcasestr(upgrade, "websocket"))
        return WS_HS_ERROR;

    char *connection = ascii_strcasestr(hs->resp, "connection:");
    if (!connection || !ascii_strcasestr(connection, "upgrade"))
        return WS_HS_ERROR;

    if (strcmp(accept, expected_key) == 0) return WS_HS_OK;
    return WS_HS_ERROR;
}

int ws_send_pong(ws_io_t *io, const uint8_t *payload, size_t len) {
    unsigned char header[14];
    unsigned char mask[4];
    size_t head_len = 0;

    if (getrandom(mask, 4, 0) != 4) return -1;

    header[head_len++] = 0x8A;

    if (len <= 125) {
        header[head_len++] = 0x80 | (unsigned char)len;
    } else if (len <= 0xFFFF) {
        header[head_len++] = 0x80 | 126;
        header[head_len++] = (len >> 8) & 0xFF;
        header[head_len++] = len & 0xFF;
    } else {
        header[head_len++] = 0x80 | 127;
        for (int i = 7; i >= 0; i--)
            header[head_len++] = (len >> (i * 8)) & 0xFF;
    }

    memcpy(header + head_len, mask, 4);
    head_len += 4;

    size_t total_len = head_len + len;
    unsigned char *frame = malloc(total_len);
    if (!frame) return -1;

    memcpy(frame, header, head_len);
    memcpy(frame + head_len, payload, len);
    mask_payload(frame + head_len, len, mask);

    size_t sent = 0;
    while (sent < total_len) {
        int n = io->write(io->ctx, frame + sent, total_len - sent);

        if (n > 0) {
            sent += n;
        } else if (n == WS_AGAIN) {
            free(frame);
            return WS_AGAIN;
        } else {
            free(frame);
            return WS_ERROR;
        }
        sleep_ms(1);
    }

    free(frame);
    return 0;
}


int ws_send_text(ws_io_t *io, const char* msg) {
    size_t len = strlen(msg);
    unsigned char header[14];
    unsigned char mask[4];
    size_t head_len = 0;

    if (getrandom(mask, 4, 0) != 4) return -1;

    header[head_len++] = 0x81;
    if (len <= 125) {
        header[head_len++] = 0x80 | (unsigned char)len;
    } else if (len <= 0xFFFF) {
        header[head_len++] = 0x80 | 126;
        header[head_len++] = (len >> 8) & 0xFF;
        header[head_len++] = len & 0xFF;
    } else {
        header[head_len++] = 0x80 | 127;
        for (int i=7; i>=0; i--) header[head_len++] = (len >> (i*8)) & 0xFF;
    }
    memcpy(header + head_len, mask, 4);
    head_len += 4;

    size_t total_len = head_len + len;
    unsigned char *frame = malloc(total_len);
    if (!frame) return -1;
    memcpy(frame, header, head_len);
    memcpy(frame + head_len, msg, len);
    mask_payload(frame + head_len, len, mask);

    size_t total_sent = 0;
    while (total_sent < total_len) {
        int n = io->write(io->ctx, frame + total_sent, total_len - total_sent);

        if (n > 0) {
            total_sent += n;
        } else if (n == WS_AGAIN) {
            free(frame);
            return WS_AGAIN;
        } else {
            free(frame);
            return WS_ERROR;
        }
    }

    free(frame);
    return (int)total_sent;
}
int ws_recv_step(ws_io_t *io, ws_recv_ctx_t *ctx, uint8_t **out, size_t *out_len, ws_thread_ctx_t *flags) {
    size_t n;
    int rc;
    (void)flags;
    while (1) {
        switch (ctx->phase) {
            case WS_RECV_HDR:
                rc = io->read(io->ctx, ctx->hdr + ctx->hdr_off, 2 - ctx->hdr_off, &n);
                if (rc != WS_OK) return rc;
                ctx->hdr_off += n;
                if (ctx->hdr_off < 2) return WS_AGAIN;

                ctx->opcode = ctx->hdr[0] & 0x0F;
                switch (ctx->opcode) {
                    case 0x0: case 0x1: case 0x2:
                    case 0x8: case 0x9: case 0xA:
                        break;
                    default:
                        return WS_ERROR;
                }
                ctx->fin = (ctx->hdr[0] & 0x80) != 0;
                ctx->opcode = ctx->hdr[0] & 0x0F;

                if ((ctx->opcode & 0x08) && !ctx->fin) {
                    ws_quit(ctx);
                    ws_clear_message(ctx);
                    return WS_ERROR;
                }


                uint8_t len7 = ctx->hdr[1] & 0x7F;
                ctx->masked = (ctx->hdr[1] & 0x80) != 0;
                if (ctx->masked) return WS_ERROR;
                if ((ctx->opcode & 0x08) && len7 > 125) return WS_ERROR;
                if (len7 <= 125) {
                    ctx->payload_len = len7;
                    ctx->payload_off = 0;
                    ctx->ext_off = 0;
                    if (ctx->payload_len > MAX_WS_PAYLOAD) {
                        return WS_PAYLOAD_TOO_BIG;
                    }
                    /* hello, ive decided to write a paragraph here about this line right under this comment
                     * so, you may be wondering, why are we checking for a mask, despite this being a client implementation, even having phases for it?
                     * the reason is simple, really, because up there, we already checked for a mask, and return an error, this is functionally dead code
                     * but my dad told me that in life, its not always about whether something is worth saving, its about the act of saving itself
                     * if we decide not to save another person, simply because they will not benefit us, is that a moral thing to do?
                     * therefore, i truly believe that if we do keep this code here, we are not harming ourselves, yet, we are able to save this piece of logic
                     * isnt that so beautiful?
                     * (also maybe ill write a server implementation later, and a palindrome checker)
                     */
                    ctx->phase = ctx->masked ? WS_RECV_MASK : WS_RECV_PAYLOAD;
                } else {
                    ctx->ext_len = (len7 == 126) ? 2 : 8;
                    ctx->ext_off = 0;
                    ctx->phase = WS_RECV_EXT_LEN;
                }

                if (ctx->masked) {
                    ctx->mask_off = 0; //sobb why are we still doing dis
                }

                break;

            case WS_RECV_EXT_LEN:
                rc = io->read(io->ctx, ctx->ext + ctx->ext_off, ctx->ext_len - ctx->ext_off, &n);
                if (rc != WS_OK) return rc;
                ctx->ext_off += n;

                if (ctx->ext_off < ctx->ext_len) return WS_AGAIN;

                ctx->payload_len = 0;
                for (size_t i = 0; i < ctx->ext_len; i++) {
                    ctx->payload_len = (ctx->payload_len << 8) | ctx->ext[i];
                }

                if (ctx->ext_len == 8 && (ctx->payload_len & (1ULL << 63))) {
                    return WS_ERROR;
                }

                if (ctx->payload_len > MAX_WS_PAYLOAD) {
                    return WS_PAYLOAD_TOO_BIG;
                }

                ctx->phase = ctx->masked ? WS_RECV_MASK : WS_RECV_PAYLOAD;
                break;

            case WS_RECV_MASK:
                rc = io->read(io->ctx, ctx->mask + ctx->mask_off, 4 - ctx->mask_off, &n);
                if (rc != WS_OK) return rc;
                ctx->mask_off += n;

                if (ctx->mask_off < 4) return WS_AGAIN;

                ctx->phase = WS_RECV_PAYLOAD;
                break;

            case WS_RECV_PAYLOAD:
                if (!ctx->payload) {
                    ctx->payload = malloc(ctx->payload_len + 1);
                    if (!ctx->payload) {
                        ws_reset(ctx);
                        return WS_ERROR;
                    }
                }
                rc = io->read(io->ctx, ctx->payload + ctx->payload_off, ctx->payload_len - ctx->payload_off, &n);
                if (rc == WS_AGAIN) return WS_AGAIN;
                if (rc != WS_OK) {
                    ws_quit(ctx);
                    return rc;
                }
                ctx->payload_off += n;

                if (ctx->payload_off < ctx->payload_len) return WS_AGAIN;

                if (ctx->masked) {
                    for (uint64_t i = 0; i < ctx->payload_len; i++)
                        ctx->payload[i] ^= ctx->mask[i & 3];
                }
                ctx->payload[ctx->payload_len] = 0;
                *out = ctx->payload;
                *out_len = ctx->payload_len;

                if (ctx->opcode == 0x08) {ws_quit(ctx); return WS_NORMAL_CLOSE;}
                if (ctx->opcode == WS_OPCODE_PING) {
                    ws_send_pong(io, ctx->payload, ctx->payload_len);
                    ws_quit(ctx);
                    return WS_AGAIN;
                }

                if (ctx->opcode == WS_OPCODE_CONTINUATION) {
                    if (!ctx->message_in_progress) {
                        ws_clear_message(ctx);
                        return WS_ERROR;
                    }
                    if (ctx->message_opcode == 0) {
                        ws_quit(ctx);
                        ws_clear_message(ctx);
                        return WS_ERROR;
                    }


                    int r = append_to_message(ctx, ctx->payload, ctx->payload_len);
                    if (r != WS_OK) {
                        ws_quit(ctx);
                        ws_clear_message(ctx);
                        return WS_ERROR;
                    }
                    free(ctx->payload);
                    ctx->payload = NULL;

                    if (ctx->fin) {
                        uint8_t *result = ctx->message_buf;

                        *out = result;
                        *out_len = ctx->message_len;

                        ctx->message_buf = NULL;
                        ctx->message_len = 0;
                        ctx->message_cap = 0;
                        ctx->message_in_progress = false;

                        ws_reset(ctx);
                        return WS_OK;
                    }

                    ws_reset(ctx);
                    return WS_AGAIN;
                }

                else if (ctx->opcode == WS_OPCODE_TEXT || ctx->opcode == WS_OPCODE_BINARY) {

                    if (ctx->message_in_progress) {
                        ws_quit(ctx);
                        ws_clear_message(ctx);
                        return WS_ERROR;
                    }


                    if (ctx->fin) {
                        *out = ctx->payload;
                        *out_len = ctx->payload_len;
                        ws_reset(ctx);
                        return WS_OK;
                    }

                    ctx->message_in_progress = true;
                    ctx->message_opcode = ctx->opcode;


                    if (!(init_message_buffer(ctx, ctx->payload, ctx->payload_len)==WS_OK)){
                        free(ctx->payload);
                        ctx->payload = NULL;
                        ws_clear_message(ctx);
                        return WS_ERROR;
                    }
                    free(ctx->payload);
                    ctx->payload = NULL;

                    ws_reset(ctx);
                    return WS_AGAIN;
                }


                ws_reset(ctx);
                return WS_OK;


            default: return WS_ERROR;
        }
    }
}

void push_json(ws_thread_ctx_t *ctx, char *json) {
    if (!json) return;

    if (rb_enqueue(&ctx->json_inbound, json) != 0) {
        free(json);
    }
}
int pre_ws_loop(ws_thread_ctx_t *flags, ws_recv_ctx_t *ws_recv_ctx) {
    int sock = flags->sock;
    (void)sock;
    (void)ws_recv_ctx;
    ws_io_t *io = &flags->io;
    char identify[64];
    snprintf(identify, sizeof(identify), "{\"user\": %d}", 1234);

    ///printf("[WS] sending auth: %s\n", identify);
    if (ws_send_text(io, identify) <= 0) {
        //fprintf(stderr, "[WS] failed to send auth \n");
        return -1;
    }
    atomic_store(&flags->running, true);
    //printf("[WS] thread entered main loop phase\n");

    return 0;
}

int ws_send_close(ws_io_t *io, uint16_t code) {
    uint8_t header[14];
    uint8_t mask[4];
    size_t head_len = 0;

    if (getrandom(mask, 4, 0) != 4)
        return -1;

    header[head_len++] = 0x88;
    header[head_len++] = 0x80 | 2;

    memcpy(header + head_len, mask, 4);
    head_len += 4;

    uint8_t payload[2];
    payload[0] = (code >> 8) & 0xFF;
    payload[1] = code & 0xFF;

    payload[0] ^= mask[0];
    payload[1] ^= mask[1];

    uint8_t frame[16];
    memcpy(frame, header, head_len);
    memcpy(frame + head_len, payload, 2);

    size_t total_len = head_len + 2;
    size_t sent = 0;

    while (sent < total_len) {
        int n = io->write(io->ctx, frame + sent, total_len - sent);

        if (n > 0) {
            sent += n;
        } else if (n == WS_AGAIN) {
            return WS_AGAIN;
        } else {
            return WS_ERROR;
        }
    }

    return 0;
}


void* websocket_thread(void *ctx) {
    ws_thread_ctx_t *flags = (ws_thread_ctx_t*)ctx;
    ws_io_t *io = &flags->io;
    ws_recv_ctx_t ws_recv_ctx = {0};
    ws_reset(&ws_recv_ctx);



    int epfd = epoll_create1(0);
    if (epfd == -1) {
        perror("epoll_create1");
        return NULL;
    }


    struct epoll_event ev = {
        .events = EPOLLIN,
        .data.fd = io->sock
    };
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, io->sock, &ev) == -1) {
        perror("epoll_ctl");
        close(epfd);
        return NULL;
    }

    if (pre_ws_loop(flags, &ws_recv_ctx) != 0) {
        fprintf(stderr, "Failed pre-loop setup\n");
        goto cleanup;
    }

    struct epoll_event events[1];

    while (atomic_load(&flags->running)) {

        int nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);

        if (nfds < 0) {
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < nfds; i++) {

            uint32_t ev = events[i].events;

            if (ev & EPOLLIN) {

                uint8_t *msg = NULL;
                size_t msg_len = 0;

                int rc = ws_recv_step(
                    &flags->io,
                    &ws_recv_ctx,
                    &msg,
                    &msg_len,
                    flags
                );

                if (rc == WS_ERROR || rc == WS_CLOSED) {
                    goto cleanup;
                }

                if (!flags->want_write) {

                    char *json = rb_dequeue(&flags->json_outbound);

                    if (json) {

                        flags->out_frame = build_frame(json, &flags->out_len);

                        flags->out_sent = 0;
                        flags->want_write = true;

                        free(json);

                        struct epoll_event mod = {
                            .events = EPOLLIN | EPOLLOUT,
                            .data.fd = flags->sock
                        };

                        epoll_ctl(epfd,
                                  EPOLL_CTL_MOD,
                                  flags->sock,
                                  &mod);
                    }
                }
            }

            if (ev & EPOLLOUT) {

                if (flags->want_write) {

                    int n = flags->io.write(
                        flags->io.ctx,
                        flags->out_frame + flags->out_sent,
                        flags->out_len - flags->out_sent
                    );

                    if (n > 0) {

                        flags->out_sent += n;

                        if (flags->out_sent == flags->out_len) {

                            free(flags->out_frame);
                            flags->out_frame = NULL;
                            flags->want_write = false;

                            struct epoll_event mod = {
                                .events = EPOLLIN,
                                .data.fd = flags->sock
                            };

                            epoll_ctl(epfd,
                                      EPOLL_CTL_MOD,
                                      flags->sock,
                                      &mod);
                        }

                    } else if (n == WS_AGAIN) {
                    } else {
                        goto cleanup;
                    }
                }
            }

            if (ev & (EPOLLERR | EPOLLHUP)) {
                goto cleanup;
            }
        }
    }

    //printf("[WS] Thread exiting...\n");
    cleanup:
    ws_quit(&ws_recv_ctx);
    ws_clear_message(&ws_recv_ctx);
    if (epfd != -1)
        close(epfd);
    return NULL;
}
