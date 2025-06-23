PROJECT=moog-ladder-filter
SRC=main.c audio.c ui.c util.c
CC=gcc
CFLAGS=-Wall -O2 -I/opt/homebrew/include
LDFLAGS=-L/opt/homebrew/lib
LIBS=-lportaudio -lpthread -lm -lncurses
OUT=$(PROJECT)

.PHONY: all clean

all: $(OUT)

$(OUT): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(OUT) $(LDFLAGS) $(LIBS)

clean:
	rm -f $(OUT)
