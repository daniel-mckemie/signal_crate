UNAME := $(shell uname)

ifeq ($(UNAME), Darwin)
	SHARED_EXT = dylib
	SHARED_FLAG = -dynamiclib
else
	SHARED_EXT = so
	SHARED_FLAG = -shared
endif

MODULE_NAME = spec_hold
MODULE_DIR = ../..

SRC = $(MODULE_NAME).c
UTIL = $(MODULE_DIR)/util.c
MODULE = $(MODULE_DIR)/module.c

PKG_CONFIG_CFLAGS := $(shell pkg-config --cflags portaudio-2.0 portmidi liblo ncurses fftw3f 2>/dev/null)
PKG_CONFIG_LIBS := $(shell pkg-config --libs portaudio-2.0 portmidi liblo ncurses fftw3f 2>/dev/null)

CC = gcc
CFLAGS = -Wall -O2 -fPIC -I$(MODULE_DIR) $(PKG_CONFIG_CFLAGS)

$(MODULE_NAME).$(SHARED_EXT): $(SRC) $(UTIL)
	$(CC) $(CFLAGS) $(SHARED_FLAG) -o $(MODULE_NAME).$(SHARED_EXT) $(SRC) $(UTIL) $(MODULE) $(PKG_CONFIG_LIBS) -lfftw3f -lpthread -lm

clean:
	rm -f *.dylib *.so
