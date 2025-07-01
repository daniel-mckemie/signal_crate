CC        = gcc
CFLAGS    = -Wall -O2 -fPIC -I../.. -I/opt/homebrew/include
LDFLAGS   = -L/opt/homebrew/lib -dynamiclib -lportaudio -lpthread -lm -lncurses

NAME      := $(notdir $(CURDIR))
SRC       = $(NAME).c util.c
OUT       = $(NAME).dylib

all: $(OUT)

$(OUT): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OUT)

.PHONY: all clean

