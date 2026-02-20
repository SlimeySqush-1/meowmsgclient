#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include "types.h"
#include <stdlib.h>

int rb_init(ws_queue_t *queue, size_t capacity);
void rb_destroy(ws_queue_t *queue);
int rb_enqueue(ws_queue_t *queue, char *message);
char *rb_dequeue(ws_queue_t *queue);

size_t base64_encode(const unsigned char *in, size_t in_len, char *out);

void sleep_ms(long ms);

void push_console_outbound(ws_thread_ctx_t *flags, const char *msg);

#endif
