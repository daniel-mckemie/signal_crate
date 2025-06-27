PROJECT=moog_filter
SRC=moog_filter.c util.c
CC=gcc
CFLAGS=-Wall -O2 -fPIC -I../.. -I/opt/homebrew/include
LDFLAGS=-L/opt/homebrew/lib -dynamiclib
LIBS=-lportaudio -lpthread -lm
OUT=$(PROJECT).dylib

.PHONY: all clean

all: $(OUT)

$(OUT): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(OUT) $(LDFLAGS) $(LIBS)

clean:
	rm -f $(OUT)

