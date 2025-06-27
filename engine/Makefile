CC=gcc

CFLAGS=-Wall -O2 -fPIC -I. -I./modules -I/opt/homebrew/include
LDFLAGS=-ldl -lportaudio -lpthread -lm -L/opt/homebrew/lib

SOURCES=main.c engine.c module_loader.c
HEADERS=module.h patch.h engine.h module_loader.h

OUT=engine

.PHONY: all clean modules

all: $(OUT)

$(OUT): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(SOURCES) -o $(OUT) $(LDFLAGS)

clean:
	rm -f $(OUT)
