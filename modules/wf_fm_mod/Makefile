CC=gcc
CFLAGS=-Wall -O2 -fPIC -I../.. -I../../.. -I/opt/homebrew/include
LDFLAGS=-dynamiclib -L/opt/homebrew/lib -lportaudio -lpthread -lm -lncurses

all: wf_fm_mod.dylib

wf_fm_mod.dylib: wf_fm_mod.c util.c
	$(CC) $(CFLAGS) wf_fm_mod.c util.c -o wf_fm_mod.dylib $(LDFLAGS)

clean:
	rm -f wf_fm_mod.dylib
