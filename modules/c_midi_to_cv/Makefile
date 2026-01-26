MODULE_NAME = c_midi_to_cv
MODULE_DIR  = ../..

SRC    = $(MODULE_NAME).c
UTIL   = $(MODULE_DIR)/util.c
MODULE = $(MODULE_DIR)/module.c

CC = gcc
CFLAGS  = -Wall -O2 -fPIC -I$(MODULE_DIR) -I/opt/homebrew/include
LDFLAGS = -dynamiclib \
          -Wl,-undefined,dynamic_lookup \
          -lpthread -lm -lncurses

$(MODULE_NAME).dylib: $(SRC) $(UTIL) $(MODULE)
	$(CC) $(CFLAGS) -o $(MODULE_NAME).dylib \
	$(SRC) $(UTIL) $(MODULE) \
	$(LDFLAGS)

clean:
	rm -f *.dylib

