UNAME := $(shell uname)

ifeq ($(UNAME), Darwin)
    SHARED_EXT = dylib
else
    SHARED_EXT = so
endif

APP = SignalCrate
SRCS = main.c engine.c scheduler.c ui.c module_loader.c util.c osc.c midi.c module.c

# Use pkg-config to get library flags, with fallback to homebrew paths
PKG_CONFIG_CFLAGS := $(shell pkg-config --cflags portaudio-2.0 portmidi liblo ncurses 2>/dev/null)
PKG_CONFIG_LIBS := $(shell pkg-config --libs portaudio-2.0 portmidi liblo ncurses 2>/dev/null)

# Fallback to homebrew paths if pkg-config fails
ifeq ($(PKG_CONFIG_LIBS),)
    ifeq ($(UNAME), Darwin)
        PKG_CONFIG_CFLAGS = -I/opt/homebrew/include
        PKG_CONFIG_LIBS = -L/opt/homebrew/lib -lportaudio -lportmidi -lncurses -llo
    else
        PKG_CONFIG_CFLAGS =
        PKG_CONFIG_LIBS = -lportaudio -lportmidi -lncurses -llo
    endif
endif

CFLAGS = -Wall -O2 -fPIC -I./modules -I. $(PKG_CONFIG_CFLAGS)
LDFLAGS = $(PKG_CONFIG_LIBS) -ldl -lpthread -lm

# === Modules ===
MODULE_DIRS := $(shell find modules -type f -name Makefile -exec dirname {} \;)
SPECIAL_TARGETS := all clean modules it

# === Targets ===
.PHONY: all modules clean

all: $(APP) modules

$(APP): $(SRCS)
	gcc $(CFLAGS) -o $(APP) $(SRCS) $(LDFLAGS)

modules:
	@for dir in $(MODULE_DIRS); do \
		echo "Building $$dir..."; \
		$(MAKE) -C $$dir || exit 1; \
	done

clean:
	rm -f $(APP)
	for dir in $(MODULE_DIRS); do \
		$(MAKE) -C $$dir clean; \
	done

it: clean all

# Build individual module with cleanup
$(filter-out $(SPECIAL_TARGETS),$(MAKECMDGOALS)):
	@modpath=modules/$@; \
	lib="$$modpath/$@.$(SHARED_EXT)"; \
	if [ -f "$$lib" ]; then \
		echo "Removing $$lib..."; \
		rm -f "$$lib"; \
	fi; \
	echo "Building module $@..."; \
	$(MAKE) -C $$modpath

