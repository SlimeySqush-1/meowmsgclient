#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include "types.h"
#include <stdlib.h>
#include "utils.h"
#include <stddef.h>

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    size_t base64_encode(const unsigned char *in, size_t in_len, char *out) {
        size_t i = 0, j = 0;

        while (i < in_len) {
            size_t remaining = in_len - i;

            unsigned octet_a = in[i++];
            unsigned octet_b = remaining > 1 ? in[i++] : 0;
            unsigned octet_c = remaining > 2 ? in[i++] : 0;

            unsigned triple = (octet_a << 16)
                            | (octet_b << 8)
                            | octet_c;

            out[j++] = b64_table[(triple >> 18) & 0x3F];
            out[j++] = b64_table[(triple >> 12) & 0x3F];

            if (remaining > 1)
                out[j++] = b64_table[(triple >> 6) & 0x3F];
            else
                out[j++] = '=';

            if (remaining > 2)
                out[j++] = b64_table[triple & 0x3F];
            else
                out[j++] = '=';
        }

        out[j] = '\0';
        return j;
    }


void sleep_ms(long ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

void push_console_outbound(ws_thread_flags_t *flags, const char *msg) {
    if (!msg) return;

    if (atomic_exchange(&flags->console_outbound_exists, true)) {
        return;
    }

    size_t len = strlen(msg) + 1;
    char *buffer = malloc(len);
    if (!buffer) {
        atomic_store(&flags->console_outbound_exists, false);
        return;
    }

    memcpy(buffer, msg, len);

    char *old = atomic_exchange(&flags->console_outbound, buffer);
    if (old) free(old);
}
