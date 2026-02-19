#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include "types.h"
#include <stdlib.h>

size_t base64_encode(const unsigned char *in, size_t in_len, char *out);

void sleep_ms(long ms);

void push_console_outbound(ws_thread_flags_t *flags, const char *msg);

#endif
