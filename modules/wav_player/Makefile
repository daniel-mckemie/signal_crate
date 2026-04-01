UNAME := $(shell uname)

ifeq ($(UNAME), Darwin)
    SHARED_EXT = dylib
    SHARED_FLAG = -dynamiclib
else
    SHARED_EXT = so
    SHARED_FLAG = -shared
endif

MODULE_NAME = wav_player
MODULE_DIR = ../..

SRC = $(MODULE_NAME).c
UTIL = $(MODULE_DIR)/util.c
MODULE = $(MODULE_DIR)/module.c

# Use pkg-config to get library flags
PKG_CONFIG_CFLAGS := $(shell pkg-config --cflags portaudio-2.0 portmidi liblo ncurses sndfile 2>/dev/null)
PKG_CONFIG_LIBS := $(shell pkg-config --libs portaudio-2.0 portmidi liblo ncurses sndfile 2>/dev/null)

CC = gcc
CFLAGS = -Wall -O2 -fPIC -I$(MODULE_DIR) $(PKG_CONFIG_CFLAGS)
LDFLAGS = $(PKG_CONFIG_LIBS) $(SHARED_FLAG) -lpthread -lm

$(MODULE_NAME).$(SHARED_EXT): $(SRC) $(UTIL)
	$(CC) $(CFLAGS) $(SHARED_FLAG) -o $(MODULE_NAME).$(SHARED_EXT) \
	$(SRC) $(UTIL) $(MODULE) $(PKG_CONFIG_LIBS) -lsndfile -lpthread -lm

clean:
	rm -f *.dylib *.so
