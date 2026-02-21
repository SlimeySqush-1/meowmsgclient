CC = clang
CFLAGS = -g -Wall -Wextra -Werror -pedantic -std=c23
CFLAGS += -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE -fsanitize=address -fno-omit-frame-pointer

LDLIBS = -lcrypto -lcurl -lcjson -lncurses -lssl -lcrypto

TARGET = meowmsgclient

SRC = $(wildcard *.c)
OBJ = $(SRC:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f $(TARGET) $(OBJ)

.PHONY: all clean
