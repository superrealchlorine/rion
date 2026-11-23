PREFIX ?= /usr/local
CC ?= cc
CFLAGS ?= -O2 -std=gnu99 -Wall -Wextra -Wno-unused-parameter
CPPFLAGS += -I./wasm3/source
LDLIBS += -lm

TARGET = rion
OBJS = rion.o \
       wasm3/source/m3_api_libc.o \
       wasm3/source/m3_api_meta_wasi.o \
       wasm3/source/m3_api_tracer.o \
       wasm3/source/m3_api_uvwasi.o \
       wasm3/source/m3_api_wasi.o \
       wasm3/source/m3_bind.o \
       wasm3/source/m3_code.o \
       wasm3/source/m3_compile.o \
       wasm3/source/m3_core.o \
       wasm3/source/m3_deterministic.o \
       wasm3/source/m3_env.o \
       wasm3/source/m3_exec.o \
       wasm3/source/m3_function.o \
       wasm3/source/m3_info.o \
       wasm3/source/m3_module.o \
       wasm3/source/m3_parse.o \
       wasm3/source/m3_snapshot.o \
       wasm3/source/m3_validate.o \
       wasm3/source/m3_xxh64.o

.SUFFIXES: .c .o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $$(sdl2-config --libs 2>/dev/null || echo -lSDL2) $$(uname -s 2>/dev/null | grep -qiE 'mingw|msys|cygwin|windows' && echo -lws2_32) $(LDLIBS)

.c.o:
	$(CC) $(CPPFLAGS) $$(sdl2-config --cflags 2>/dev/null) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(TARGET).exe $(OBJS)

install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/

.PHONY: all clean install
