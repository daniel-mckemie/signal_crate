CC = gcc
CFLAGS = -Wall -O2 -fPIC -I./modules -I/opt/homebrew/include
LDFLAGS = -L/opt/homebrew/lib -ldl -lportaudio -lpthread -lm -lncurses
SRC = main.c engine.c module_loader.c ui.c
BIN = engine

MODULE_DIR = modules
MODULES := $(shell find $(MODULE_DIR) -mindepth 1 -maxdepth 1 -type d)

all: $(BIN) modules

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

modules:
	@for dir in $(MODULES); do \
		echo "Building $$dir..."; \
		$(MAKE) -C $$dir; \
	done

clean:
	rm -f $(BIN)
	@for dir in $(MODULES); do \
		echo "Cleaning $$dir..."; \
		$(MAKE) -C $$dir clean; \
	done

.PHONY: all modules clean

