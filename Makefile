CC = gcc
CFLAGS = -Wall -O2 -fPIC -I./modules -I/opt/homebrew/include
LDFLAGS = -L/opt/homebrew/lib -ldl -lportaudio -lpthread -lm -lncurses

engine: main.o engine.o module_loader.o engine_ui.o
	$(CC) -o engine main.o engine.o module_loader.o engine_ui.o $(LDFLAGS)

main.o: main.c engine.h module_loader.h ui.h
	$(CC) $(CFLAGS) -c -o main.o main.c

engine.o: engine.c engine.h module_loader.h
	$(CC) $(CFLAGS) -c -o engine.o engine.c

module_loader.o: module_loader.c module_loader.h engine.h
	$(CC) $(CFLAGS) -c -o module_loader.o module_loader.c

engine_ui.o: ui.c ui.h engine.h
	$(CC) $(CFLAGS) -c -o engine_ui.o ui.c

clean:
	rm -f *.o engine

.PHONY: clean
