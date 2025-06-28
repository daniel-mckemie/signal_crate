CC = gcc
CFLAGS = -Wall -O2 -fPIC -I../.. -I../../.. -I/opt/homebrew/include
LDFLAGS = -dynamiclib -L/opt/homebrew/lib -lportaudio -lpthread -lm -lncurses

all: vco.dylib

vco.dylib: vco.c util.c
	$(CC) $(CFLAGS) vco.c util.c -o vco.dylib $(LDFLAGS)

clean:
	rm -f vco.dylib

