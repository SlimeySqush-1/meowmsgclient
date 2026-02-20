#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include "types.h"
#include <stdlib.h>
#include "utils.h"
#include <stddef.h>

int rb_init(ws_queue_t *queue, size_t capacity) {
    queue->message = malloc(capacity * sizeof(char *));
    if (!queue->message) {
        return -1;
    }

    queue->capacity = capacity;
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;

    if (pthread_mutex_init(&queue->lock, NULL) != 0) {
        free(queue->message);
        return -1;
    }

    return 0;
}

void rb_destroy(ws_queue_t *queue) {
    pthread_mutex_destroy(&queue->lock);
    free(queue->message);
}

int rb_enqueue(ws_queue_t *queue, char *message) {
    pthread_mutex_lock(&queue->lock);

    if (queue->count == queue->capacity) {
        pthread_mutex_unlock(&queue->lock);
        return -1;
    }

    queue->message[queue->tail] = message;
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count++;

    pthread_mutex_unlock(&queue->lock);
    return 0;
}

char *rb_dequeue(ws_queue_t *queue) {
    pthread_mutex_lock(&queue->lock);
    if (queue->count == 0) {
        pthread_mutex_unlock(&queue->lock);
        return NULL;
    }
    char *message = queue->message[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;
    pthread_mutex_unlock(&queue->lock);
    return message;
}

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

void push_console_outbound(ws_thread_ctx_t *ctx, const char *msg) {
    if (!msg) return;

    size_t len = strlen(msg) + 1;
    char *buffer = malloc(len);
    if (!buffer) return;

    memcpy(buffer, msg, len);

    if (rb_enqueue(&ctx->console_outbound, buffer) != 0) {
        free(buffer);
    }
}
