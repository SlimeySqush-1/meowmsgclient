#include <curl/curl.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>

#include "tcp.h"
#include "tls.h"
#include "ws.h"
#include "types.h"
#include "json.h"
#include "interface.h"
#include "utils.h"

#define PORT 8080
#define HOST "127.0.0.1"
#define RB_SIZE 4096

int main(void) {

    curl_global_init(CURL_GLOBAL_ALL);

    int sock = tcp_connect(HOST, PORT);
    if (sock < 0) {
        perror("TCP connect");
        return 1;
    }

    printf("tcp connected\n");

    bool use_tls = true;
    SSL_CTX *ctx = NULL;
    SSL *ssl = NULL;

    ctx = tls_init();
    ssl = tls_connect(ctx, sock, HOST);

    if (ssl == NULL) {
        fprintf(stderr, "TLS connect failed, falling back to plain TCP\n");

        close(sock);
        tls_cleanup(ctx, NULL);

        sock = tcp_connect(HOST, PORT);
        if (sock < 0) {
            perror("TCP reconnect failed");
            return 1;
        }

        use_tls = false;
    } else {
        printf("tls connected\n");
    }


    ws_io_t io;
    memset(&io, 0, sizeof(io));

    if (use_tls) {
        io.encrypted = true;
        io.read = tls_read;
        io.write = tls_write;
        io.ctx = ssl;
    } else {
        io.encrypted = false;
        io.read = tcp_read;
        io.write = tcp_write;
        io.ctx = &sock;
    }

    ws_handshake_t hs;
    memset(&hs, 0, sizeof(hs));

    while (1) {
        ws_hs_rc_t rc = ws_handshake(&io, HOST, "/", &hs);

        if (rc == WS_HS_OK)
            break;

        if (rc == WS_HS_ERROR) {
            fprintf(stderr, "WS handshake failed\n");
            goto cleanup;
        }

        sleep_ms(1);
    }

    printf("ws handshake done\n");


    ws_thread_ctx_t ws_flags;
    memset(&ws_flags, 0, sizeof(ws_flags));

    ws_flags.sock = sock;
    ws_flags.running = true;
    ws_flags.io = io;

    if (rb_init(&ws_flags.json_inbound, RB_SIZE) != 0 ||
        rb_init(&ws_flags.json_outbound, RB_SIZE) != 0 ||
        rb_init(&ws_flags.console_outbound, RB_SIZE) != 0) {

        fprintf(stderr, "Failed to initialize ring buffers\n");
        goto cleanup;
    }

    pthread_t wst;
    pthread_t json_thread;
    pthread_t interfacet;

    if (pthread_create(&wst, NULL, websocket_thread, &ws_flags) != 0 ||
        pthread_create(&json_thread, NULL, get_event_thread, &ws_flags) != 0 ||
        pthread_create(&interfacet, NULL, interface, &ws_flags) != 0) {

        fprintf(stderr, "Failed to create threads\n");
        goto cleanup;
    }

    pthread_join(wst, NULL);
    pthread_join(json_thread, NULL);
    pthread_join(interfacet, NULL);

cleanup:

    rb_destroy(&ws_flags.console_outbound);
    rb_destroy(&ws_flags.json_inbound);
    rb_destroy(&ws_flags.json_outbound);

    if (use_tls && ssl != NULL) {
        tls_cleanup(ctx, ssl);
    }

    close(sock);
    curl_global_cleanup();

    return 0;
}
