APP = FlowControl
SRCS = main.c engine.c ui.c module_loader.c util.c
CFLAGS = -Wall -O2 -fPIC -I./modules -I. -I/opt/homebrew/include
LDFLAGS = -L/opt/homebrew/lib -ldl -lportaudio -lpthread -lm -lncurses

# === Modules ===
MODULE_DIRS = modules/vco modules/moog_filter modules/wf_fm_mod modules/ring_mod

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

