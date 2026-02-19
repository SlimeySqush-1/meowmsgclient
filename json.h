#ifndef JSON_H
#define JSON_H

#include "types.h"

//A function that process the opcode, especially opcode 0 and its events
//
//flags: A pointer to the flags struct.
void get_event(ws_thread_flags_t *flags);

//Thread shii or som idk
//
//flagsv: A pointer to the flags structure (the reason why its void* is because some weird stuff happens when u dont use void* with pthread)
void* get_event_thread(void *flagsv);

#endif
