#include "types.h"
#include "utils.h"
#include <ncurses.h>
#include <stdatomic.h>
#include <string.h>
#include <cjson/cJSON.h>
#include <stdlib.h>

#define MAX_INPUT 256
#define MAX_CHANNEL_NAME 21
#define HISTORY_SIZE 50

char *format_msg(const char *channel, const char *message) {
    if (!channel || !message)
        return NULL;

    char *result = NULL;

    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON *sendMessage = cJSON_CreateObject();
    if (!sendMessage) {
        cJSON_Delete(root);
        return NULL;
    }

    cJSON_AddItemToObject(root, "SendMessage", sendMessage);

    if (!cJSON_AddStringToObject(sendMessage, "channel", channel) ||
        !cJSON_AddStringToObject(sendMessage, "message", message)) {
        cJSON_Delete(root);
        return NULL;
    }

    result = cJSON_PrintUnformatted(root);

    cJSON_Delete(root);
    return result;
}

void push_json_outbound(ws_thread_flags_t *flags, const char *json) {
    size_t len = strlen(json) + 1;
    char *buffer = malloc(len);
    if (!buffer) return;
    memcpy(buffer, json, len);

    char *old = atomic_exchange(&flags->json_outbound, buffer);
    atomic_store(&flags->json_outbound_exists, true);
    if (old) free(old);
}

void push_message(char *chn, char *msg, ws_thread_flags_t *flags) {
    if (msg && !atomic_load(&flags->json_outbound_exists)) {
        char *json = format_msg(chn, msg);
        if (!json) return;
        push_json_outbound(flags, json);
        free(json);
    } else if (msg) {
        printf("[IF] mailbox full, skipping\n");
    }
}

void inject_input(const char *input, char input_buffer[MAX_INPUT], int *input_ptr) {
    size_t len = strlen(input);

    if (len >= MAX_INPUT) {
        printf("Input too long\n");
        return;
    }

    strcpy(input_buffer, input);
    *input_ptr = (int)len;
}

void history_add(char history[HISTORY_SIZE][MAX_INPUT],
                 int *history_count,
                 const char *input)
{
    if (strlen(input) == 0) return;

    int index = *history_count % HISTORY_SIZE;

    strncpy(history[index], input, MAX_INPUT - 1);
    history[index][MAX_INPUT - 1] = '\0';

    (*history_count)++;
}


void* interface(void *ctx) {
    ws_thread_flags_t *flags = (ws_thread_flags_t *)ctx;
    WINDOW *msg_win, *input_win;
    int height, width;

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);

    getmaxyx(stdscr, height, width);

    msg_win = newwin(height - 3, width, 0, 0);
    scrollok(msg_win, TRUE);

    input_win = newwin(3, width, height - 3, 0);
    keypad(input_win, TRUE);
    nodelay(input_win, TRUE);

    char input_buffer[MAX_INPUT] = {0};
    int input_ptr = 0;
    char current_channel[MAX_CHANNEL_NAME] = "d670lujn2hn36aidb7dg";


    char history[HISTORY_SIZE][MAX_INPUT];
    int history_count = 0;        // total stored
    int history_index = -1;       // navigation position
    char draft_buffer[MAX_INPUT];
    int draft_len = 0;
    (void)draft_len;


    while (atomic_load(&flags->running)) {
        if (atomic_load(&flags->console_outbound_exists)) {
            char *msg = atomic_exchange(&flags->console_outbound, NULL);
            if (msg != NULL) {
                wprintw(msg_win, "remote: %s\n", msg);
                free(msg);
                atomic_store(&flags->console_outbound_exists, false);
                wrefresh(msg_win);
            }
        }

        int ch = wgetch(input_win);

        if (ch != ERR) {
            if (ch == '\n' || ch == KEY_ENTER) {

                if (input_ptr > 0) {
                    input_buffer[input_ptr] = '\0';
                    wprintw(msg_win, "%s\n", input_buffer);
                    if (input_buffer[0] == '/') {
                        if (strcmp(input_buffer, "/quit") == 0) {
                            atomic_store(&flags->running, false);
                        } else if (strncmp(input_buffer, "/sendraw ", 9) == 0){
                            char *raw_message = malloc(MAX_INPUT);
                            strncpy(raw_message, input_buffer + 9, MAX_INPUT - 9);
                            push_json_outbound(flags, raw_message);
                            wprintw(msg_win, "you (raw): %s\n", raw_message); //rawr
                            free(raw_message);
                        } else if (strncmp(input_buffer, "/join ", 6) == 0) {
                            strncpy(current_channel, input_buffer + 6, MAX_CHANNEL_NAME - 1);
                            wprintw(msg_win, "Switched to: %s\n", current_channel);
                        }
                    } else {
                        push_message(current_channel, input_buffer, flags);
                        wprintw(msg_win, "you: %s\n", input_buffer);
                    }
                    history_add(history, &history_count, input_buffer);
                    history_index = -1;
                    input_ptr = 0;
                    memset(input_buffer, 0, MAX_INPUT);
                }
            }
            else if (ch == KEY_BACKSPACE || ch == 127 || ch == '\b') {
                if (input_ptr > 0) {
                    input_ptr--;
                    input_buffer[input_ptr] = '\0';
                }
            }
            else if (ch >= 32 && ch <= 126 && input_ptr < MAX_INPUT - 1) {
                input_buffer[input_ptr++] = ch;
            } else if (ch == KEY_UP) {
                if (history_count > 0) {
                    if (history_index == -1) {
                        strcpy(draft_buffer, input_buffer);
                        draft_len = input_ptr;

                        history_index = history_count - 1;
                    } else if (history_index > 0) {
                        history_index--;
                    }

                    int index = history_index % HISTORY_SIZE;
                    inject_input(history[index], input_buffer, &input_ptr);
                }
            } else if (ch == KEY_DOWN) {
                if (history_index != -1) {
                    if (history_index < history_count - 1) {
                        history_index++;
                        int index = history_index % HISTORY_SIZE;
                        inject_input(history[index], input_buffer, &input_ptr);
                    } else {
                        history_index = -1;
                        inject_input(draft_buffer, input_buffer, &input_ptr);
                    }
                }
            }

            wrefresh(msg_win);
            werase(input_win);
            box(input_win, 0, 0);
            mvwprintw(input_win, 1, 2, "> %s", input_buffer);
            wrefresh(input_win);

        }
        wmove(input_win, 1, 4 + input_ptr);
        napms(10);
    }

    endwin();
    return NULL;
}
