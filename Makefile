CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99 -I./wasm3/source
LIBS = $(shell sdl2-config --cflags --libs) -lm
TARGET = rion
PREFIX = /usr/local

SRCS = rion.c $(wildcard wasm3/source/*.c)

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS) $(LIBS)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/

.PHONY: all clean install
