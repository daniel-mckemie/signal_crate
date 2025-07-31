MODULE_NAME = freeverb
MODULE_DIR = ../..

SRC = $(MODULE_NAME).c
UTIL = $(MODULE_DIR)/util.c
MODULE = $(MODULE_DIR)/module.c

CC = gcc
CFLAGS = -Wall -O2 -fPIC -I$(MODULE_DIR) -I/opt/homebrew/include
LDFLAGS = -L/opt/homebrew/lib -dynamiclib -lportaudio -lpthread -lm -lncurses

$(MODULE_NAME).dylib: $(SRC) $(UTIL)
	$(CC) $(CFLAGS) -o $(MODULE_NAME).dylib $(SRC) $(UTIL) $(MODULE) $(LDFLAGS)

clean:
	rm -f *.dylib

