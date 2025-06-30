CC=gcc
CFLAGS=-Wall -O2 -fPIC -I../.. -I../../.. -I/opt/homebrew/include
LDFLAGS=-dynamiclib -L/opt/homebrew/lib -lportaudio -lpthread -lm -lncurses

all: moog_filter.dylib

moog_filter.dylib: moog_filter.c util.c
	$(CC) $(CFLAGS) moog_filter.c util.c -o moog_filter.dylib $(LDFLAGS)

clean:
	rm -f moog_filter.dylib


