CC       ?= cc
CFLAGS   ?= -O2 -std=gnu99 -Wall -Wextra -Wno-unused-parameter
SDL_CFLAGS != sdl2-config --cflags 2>/dev/null || true
SDL_LIBS   != sdl2-config --libs 2>/dev/null || echo -lSDL2
WIN_LIBS   != uname -s 2>/dev/null | grep -qiE 'mingw|msys|cygwin|windows' && echo -lws2_32 || true

CPPFLAGS += -I./wasm3/source -DMAP_NORESERVE=0 $(SDL_CFLAGS)
LDFLAGS  ?=
LDLIBS   ?= $(SDL_LIBS) $(WIN_LIBS) -lm

PREFIX   ?= /usr/local
BINDIR   ?= $(PREFIX)/bin

WASM3_OBJS = \
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

APP_OBJS = main.o stream.o assets.o video.o input.o
TARGET   = rion

.PHONY: all install clean

all: $(TARGET)

$(TARGET): $(APP_OBJS) $(WASM3_OBJS)
	$(CC) $(LDFLAGS) -o $@ $(APP_OBJS) $(WASM3_OBJS) $(LDLIBS)

.c.o:
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -s -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

clean:
	rm -f $(TARGET) $(TARGET).exe $(APP_OBJS) $(WASM3_OBJS)
