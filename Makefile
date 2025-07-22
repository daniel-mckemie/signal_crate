APP = SignalCrate
SRCS = main.c engine.c ui.c module_loader.c util.c osc.c
CFLAGS = -Wall -O2 -fPIC -I./modules -I. -I/opt/homebrew/include
LDFLAGS = -L/opt/homebrew/lib -ldl -lportaudio -lpthread -lm -lncurses -llo

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
	dylib="$$modpath/$@.dylib"; \
	if [ -f "$$dylib" ]; then \
		echo "Removing $$dylib..."; \
		rm -f "$$dylib"; \
	fi; \
	echo "Building module $@..."; \
	$(MAKE) -C $$modpath

