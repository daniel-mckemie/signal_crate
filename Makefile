UNAME := $(shell uname)

ifeq ($(UNAME), Darwin)
    SHARED_EXT = dylib
else
    SHARED_EXT = so
endif

APP  = SignalCrate
CC   = gcc

SRCS = main.c engine.c scheduler.c ui.c module_loader.c util.c osc.c midi.c module.c

PKG_CFLAGS := $(shell pkg-config --cflags portaudio-2.0 sndfile fftw3 liblo ncurses)
PKG_LIBS   := $(shell pkg-config --libs   portaudio-2.0 sndfile fftw3 liblo ncurses)

PORTMIDI_CFLAGS :=
PORTMIDI_LDFLAGS := -lportmidi

ifeq ($(UNAME), Darwin)
    PORTMIDI_PREFIX := $(shell brew --prefix portmidi 2>/dev/null)
    ifneq ($(PORTMIDI_PREFIX),)
        PORTMIDI_CFLAGS += -I$(PORTMIDI_PREFIX)/include
        PORTMIDI_LDFLAGS = -L$(PORTMIDI_PREFIX)/lib -lportmidi
    endif
endif

CFLAGS  = -Wall -O2 -fPIC -I./modules -I. $(PKG_CFLAGS) $(PORTMIDI_CFLAGS)
LDFLAGS = $(PKG_LIBS) $(PORTMIDI_LDFLAGS) -ldl -lpthread -lm

MODULE_DIRS := $(shell find modules -type f -name Makefile -exec dirname {} \;)
SPECIAL_TARGETS := all clean modules it

.PHONY: all modules clean

all: $(APP) modules

$(APP): $(SRCS)
	$(CC) $(CFLAGS) -o $(APP) $(SRCS) $(LDFLAGS)

modules:
	@for dir in $(MODULE_DIRS); do \
		echo "Building $$dir..."; \
		$(MAKE) -C $$dir || exit 1; \
	done

clean:
	rm -f $(APP)
	@for dir in $(MODULE_DIRS); do \
		$(MAKE) -C $$dir clean; \
	done

it: clean all

$(filter-out $(SPECIAL_TARGETS),$(MAKECMDGOALS)):
	@modpath=modules/$@; \
	lib="$$modpath/$@.$(SHARED_EXT)"; \
	rm -f "$$lib"; \
	echo "Building module $@..."; \
	$(MAKE) -C $$modpath
