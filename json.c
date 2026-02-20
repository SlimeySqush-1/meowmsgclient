//ununused
#include <stdatomic.h>
#include <stdio.h>
#include "json.h"
#include "types.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include <cjson/cJSON.h>
#include <unistd.h>
#include "utils.h"

//get event and shi idk
void get_event(ws_thread_ctx_t *ctx) {
    char *json_text = rb_dequeue(&ctx->json_inbound);
    if (!json_text) {
        return;
    }

    cJSON *root = cJSON_Parse(json_text);
    if (!root) {
        free(json_text);
        return;
    }

    push_console_outbound(ctx, json_text);

    free(json_text);
    cJSON_Delete(root);
}

void* get_event_thread(void *flagsv){
    ws_thread_ctx_t *flags = flagsv;

    while (atomic_load(&flags->running)){
        get_event(flags);

        sleep_ms(10);
    }
    return NULL;
}
