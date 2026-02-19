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
void get_event(ws_thread_flags_t *flags) {
    char *json_text = atomic_exchange(&flags->json_inbound, NULL);
    if (!json_text) {
        atomic_store(&flags->json_inbound_exists, 0);
        return;
    }
    cJSON *root = cJSON_Parse(json_text);
    if (!root) {
        atomic_store(&flags->json_inbound_exists, 0);
        return;
    }
    push_console_outbound(flags, json_text);
    atomic_store(&flags->json_inbound_exists, 0);
    free(json_text);
    cJSON_Delete(root);
    return;
}

void* get_event_thread(void *flagsv){
    ws_thread_flags_t *flags = flagsv;

    while (atomic_load(&flags->running)){
        get_event(flags);

        sleep_ms(10);
    }
    return NULL;
}
