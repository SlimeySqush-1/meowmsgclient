//ngl atp i may js need to use python or som
#include <curl/curl.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

#include "tcp.h"
#include "tls.h"
#include "ws.h"
#include "types.h"
#include "json.h"
#include "interface.h"

#define PORT 443
#define HOST "echo.websocket.org"
#define RB_SIZE 4096

#include "utils.h"

int main(void) {

    ws_io_t io;
    io.encrypted = false;
    io.read = NULL;
    io.write = NULL;
    io.ctx = NULL;

    curl_global_init(CURL_GLOBAL_ALL);

    int sock = tcp_connect(HOST, PORT);

    if (sock < 0) { perror("TCP connect"); return 1; }
    printf("tcp connected\n");
    SSL_CTX *ctx = tls_init();
    SSL *ssl = tls_connect(ctx, sock, HOST);

    if (ssl == NULL) {
        fprintf(stderr, "TLS connect failed\n");
        return 1;
    }
    printf("tls connected\n");
    io.encrypted = true;
    io.read = tls_read;
    io.write = tls_write;
    io.ctx = ssl;

    if (ctx == NULL) {
        fprintf(stderr, "Failed to initialize TLS context\n");
        io.encrypted = false;
        io.ctx = ctx;
        return 1;
    } else {
        io.encrypted = true;
        io.ctx = ctx;
    }

    ws_handshake_t hs;
    memset(&hs, 0, sizeof(hs));

    while (1) {
        ws_hs_rc_t rc = ws_handshake(&io, HOST, "/", &hs);
        if (rc == WS_HS_OK)
            break;

        if (rc == WS_HS_ERROR) {
            fprintf(stderr, "WS handshake failed\n");
            return 1;
        }

        sleep(1); //why sleep? idek
    }
    printf("ws handshake done\n");
    ws_thread_ctx_t ws_flags;
    ws_flags.sock = sock;
    ws_flags.running = true;
    if (rb_init(&ws_flags.json_inbound, RB_SIZE) != 0) {
        fprintf(stderr, "Failed to initialize JSON inbound queue\n");
        return 1;
    }
    if (rb_init(&ws_flags.json_outbound, RB_SIZE) != 0) {
        fprintf(stderr, "Failed to initialize JSON outbound queue\n");
        return 1;
    }
    if (rb_init(&ws_flags.console_outbound, RB_SIZE) != 0) {
        fprintf(stderr, "Failed to initialize console outbound queue\n");
        return 1;
    }
    ws_flags.close_code = 0;
    ws_flags.out_frame = NULL;
    ws_flags.want_write = false;
    ws_flags.io = io;
    if (ws_flags.io.encrypted) {
        ws_flags.io.read = tls_read;
        ws_flags.io.write = tls_write;
        ws_flags.io.ctx = ssl;
    } else {
        ws_flags.io.read = tcp_read;
        ws_flags.io.write = tcp_write;
        ws_flags.io.ctx = &ws_flags.sock;
    }


    pthread_t wst;
    pthread_t json_thread;
    pthread_t interfacet;

    pthread_create(&wst, NULL, websocket_thread, &ws_flags);
    pthread_create(&json_thread, NULL, get_event_thread, &ws_flags);
    pthread_create(&interfacet, NULL, interface, &ws_flags);

    pthread_join(wst, NULL);
    pthread_join(json_thread, NULL);
    pthread_join(interfacet, NULL);
    //printf("Exiting...\n");
    curl_global_cleanup();
    rb_destroy(&ws_flags.console_outbound);
    rb_destroy(&ws_flags.json_inbound);
    rb_destroy(&ws_flags.json_outbound);
    tls_cleanup(ctx, io.ctx);
    close(sock);
    return 0;
}
