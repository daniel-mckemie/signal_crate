UNAME := $(shell uname)

ifeq ($(UNAME), Darwin)
    SHARED_EXT = dylib
    SHARED_FLAG = -dynamiclib
else
    SHARED_EXT = so
    SHARED_FLAG = -shared
endif

MODULE_NAME = lua_box
MODULE_DIR  = ../..

SRC    = $(MODULE_NAME).c
UTIL   = $(MODULE_DIR)/util.c
MODULE = $(MODULE_DIR)/module.c

# Lua detection: try pkg-config for a few common module names, then fall back.
# macOS Homebrew: `brew install lua` provides lua (5.4).
# Ubuntu/Debian:  `apt install liblua5.4-dev` provides lua5.4.
LUA_PKG := $(shell pkg-config --exists lua      2>/dev/null && echo lua)
ifeq ($(LUA_PKG),)
    LUA_PKG := $(shell pkg-config --exists lua5.4 2>/dev/null && echo lua5.4)
endif
ifeq ($(LUA_PKG),)
    LUA_PKG := $(shell pkg-config --exists lua-5.4 2>/dev/null && echo lua-5.4)
endif
ifeq ($(LUA_PKG),)
    LUA_PKG := $(shell pkg-config --exists lua5.3 2>/dev/null && echo lua5.3)
endif

ifneq ($(LUA_PKG),)
    LUA_CFLAGS := $(shell pkg-config --cflags $(LUA_PKG))
    LUA_LIBS   := $(shell pkg-config --libs   $(LUA_PKG))
else
    # Fallback — assume a system-installed liblua without a .pc file.
    LUA_CFLAGS :=
    LUA_LIBS   := -llua
endif

PKG_CONFIG_CFLAGS := $(shell pkg-config --cflags portaudio-2.0 portmidi liblo ncurses 2>/dev/null)
PKG_CONFIG_LIBS   := $(shell pkg-config --libs   portaudio-2.0 portmidi liblo ncurses 2>/dev/null)

CC = gcc
CFLAGS = -Wall -O2 -fPIC -I$(MODULE_DIR) $(PKG_CONFIG_CFLAGS) $(LUA_CFLAGS)

ifeq ($(UNAME), Darwin)
    LDFLAGS = $(PKG_CONFIG_LIBS) $(LUA_LIBS) $(SHARED_FLAG) -undefined dynamic_lookup -lpthread -lm
else
    LDFLAGS = $(PKG_CONFIG_LIBS) $(LUA_LIBS) $(SHARED_FLAG) -lpthread -lm
endif

$(MODULE_NAME).$(SHARED_EXT): $(SRC) $(UTIL)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(MODULE_NAME).$(SHARED_EXT) \
	$(SRC) $(UTIL) $(MODULE)

clean:
	rm -f *.dylib *.so
