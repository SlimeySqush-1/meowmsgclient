//ngl atp i may js need to use python or som
#include <curl/curl.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

#include "tcp.h"
#include "ws.h"
#include "types.h"
#include "json.h"
#include "interface.h"

#define PORT 8080
#define HOST "127.0.0.1"
#define RB_SIZE 4096

#include "utils.h"

int main(void) {
    curl_global_init(CURL_GLOBAL_ALL);

    int sock = tcp_connect(HOST, PORT);
    if (sock < 0) { perror("TCP connect"); return 1; }

    ws_handshake_t hs;
    memset(&hs, 0, sizeof(hs));

    while (1) {
        ws_hs_rc_t rc = ws_handshake(sock, HOST, "/", &hs);
        if (rc == WS_HS_OK)
            break;

        if (rc == WS_HS_ERROR) {
            fprintf(stderr, "WS handshake failed\n");
            return 1;
        }

        sleep(1); //why sleep? idek
    }

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
    //tls_cleanup(ctx, ssl);
    close(sock);
    return 0;
}
