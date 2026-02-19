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

    ws_thread_flags_t ws_flags;
    ws_flags.sock = sock;
    ws_flags.running = true;
    ws_flags.json_inbound_exists = 0;
    ws_flags.json_outbound_exists = 0;
    ws_flags.console_outbound_exists = 0;
    ws_flags.console_outbound = NULL;
    ws_flags.json_inbound = NULL;
    ws_flags.json_outbound = NULL;
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

    curl_global_cleanup();
    //tls_cleanup(ctx, ssl);
    close(sock);
    return 0;
}
