#ifndef RION_H
#define RION_H

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define SOCK_INVALID INVALID_SOCKET
#define sock_close(s) closesocket(s)
#define sock_interrupted() (WSAGetLastError() == WSAEINTR)
#else
#include <errno.h>
#include <unistd.h>
typedef int sock_t;
#define SOCK_INVALID (-1)
#define sock_close(s) close(s)
#define sock_interrupted() (errno == EINTR)
#endif

#include <stddef.h>
#include <stdint.h>

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include "wasm3.h"

#define DEFAULT_HOST            "127.0.0.1"
#define DEFAULT_PORT            "49130"
#define CHLORIDE_MAGIC          0x43U
#define CHLORIDE_HDR_SIZE       8U

#define FRAME_HANDSHAKE         0x01U
#define FRAME_CODE_STREAM       0x02U
#define FRAME_ASSET_STREAM      0x03U
#define FRAME_EVENT_INPUT       0x04U
#define FRAME_TERMINATE         0x05U

#define DEFAULT_MAX_PAYLOAD     (4U * 1024U * 1024U)
#define MAX_WASM_MEMORY         (16U * 1024U * 1024U)
#define MAX_FRAME_BUDGET_MS     2000U

#define CANVAS_WIDTH            320
#define CANVAS_HEIGHT           240

typedef struct {
    char *name;
    uint8_t *data;
    uint32_t size;
} RionAsset;

typedef struct {
    RionAsset *items;
    size_t count;
    size_t cap;
} RionAssets;

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *framebuffer;
    uint64_t perf_freq;
} RionVideo;

#define INPUT_QUEUE_CAP 64

typedef struct {
    uint8_t type;
    uint8_t button;
    uint16_t mods;
    int32_t code;
    int32_t x;
    int32_t y;
} RionInputEvent;

typedef struct {
    int quit_requested;
    RionInputEvent queue[INPUT_QUEUE_CAP];
    size_t head;
    size_t tail;
    size_t count;
} RionInput;

typedef struct {
    RionAssets assets;
    RionVideo video;
    RionInput input;
} RionState;

#endif /* RION_H */
