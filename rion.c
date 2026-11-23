#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>
#endif

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#ifdef _WIN32
typedef SOCKET sock_t;
#define SOCK_INVALID INVALID_SOCKET
#define sock_close(s) closesocket(s)
#define sock_interrupted() (WSAGetLastError() == WSAEINTR)
#else
typedef int sock_t;
#define SOCK_INVALID (-1)
#define sock_close(s) close(s)
#define sock_interrupted() (errno == EINTR)
#endif

static SDL_Texture *g_texture;
static SDL_Renderer *g_renderer;

static uint16_t
read_be16(const uint8_t *b)
{
    return ((uint16_t)b[0] << 8) | (uint16_t)b[1];
}

static uint32_t
read_be32(const uint8_t *b)
{
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8)  |  (uint32_t)b[3];
}

static void
write_be16(uint8_t *b, uint16_t v)
{
    b[0] = (uint8_t)(v >> 8);
    b[1] = (uint8_t)(v & 0xFFU);
}

static void
write_be32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)(v >> 24);
    b[1] = (uint8_t)((v >> 16) & 0xFFU);
    b[2] = (uint8_t)((v >> 8) & 0xFFU);
    b[3] = (uint8_t)(v & 0xFFU);
}

static int
valid_port(const char *s)
{
    char *endp;
    long v;

    if (s == NULL || *s == '\0')
        return 0;
    errno = 0;
    v = strtol(s, &endp, 10);
    return (errno == 0 && *endp == '\0' && v >= 1 && v <= 65535);
}

static int
parse_endpoint(const char *arg, const char *port_override,
    char *host, size_t host_sz, char *port, size_t port_sz,
    char *path, size_t path_sz)
{
    const char *p, *slash, *auth_end, *colon;
    size_t len;

    snprintf(host, host_sz, "%s", DEFAULT_HOST);
    snprintf(port, port_sz, "%s", DEFAULT_PORT);
    snprintf(path, path_sz, "/");

    if (arg != NULL && strncmp(arg, "chloride://", 11) == 0) {
        p = arg + 11;
        slash = strchr(p, '/');
        auth_end = (slash != NULL) ? slash : (p + strlen(p));
        if (slash != NULL && snprintf(path, path_sz, "%s", slash) >= (int)path_sz)
            return -1;

        if (*p == '[') {
            const char *rb = memchr(p, ']', (size_t)(auth_end - p));
            if (rb == NULL || rb == p + 1)
                return -1;
            len = (size_t)(rb - (p + 1));
            if (len >= host_sz)
                return -1;
            memcpy(host, p + 1, len);
            host[len] = '\0';
            if (rb + 1 < auth_end) {
                if (rb[1] != ':')
                    return -1;
                len = (size_t)(auth_end - (rb + 2));
                if (len == 0 || len >= port_sz)
                    return -1;
                memcpy(port, rb + 2, len);
                port[len] = '\0';
            }
        } else {
            colon = memchr(p, ':', (size_t)(auth_end - p));
            if (colon != NULL) {
                len = (size_t)(colon - p);
                if (len == 0 || len >= host_sz)
                    return -1;
                memcpy(host, p, len);
                host[len] = '\0';
                len = (size_t)(auth_end - (colon + 1));
                if (len == 0 || len >= port_sz)
                    return -1;
                memcpy(port, colon + 1, len);
                port[len] = '\0';
            } else {
                len = (size_t)(auth_end - p);
                if (len == 0 || len >= host_sz)
                    return -1;
                memcpy(host, p, len);
                host[len] = '\0';
            }
        }
    } else if (arg != NULL) {
        if (*arg == '\0' || snprintf(host, host_sz, "%s", arg) >= (int)host_sz)
            return -1;
    }

    if (port_override != NULL) {
        if (snprintf(port, port_sz, "%s", port_override) >= (int)port_sz)
            return -1;
    }

    return valid_port(port) ? 0 : -1;
}

static sock_t
tcp_connect(const char *host, const char *port)
{
    struct addrinfo hints, *res, *ai;
    sock_t fd = SOCK_INVALID;
    int err;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((err = getaddrinfo(host, port, &hints, &res)) != 0) {
        fprintf(stderr, "rion: getaddrinfo(%s:%s): %s\n",
            host, port, gai_strerror(err));
        return SOCK_INVALID;
    }

    for (ai = res; ai != NULL; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd == SOCK_INVALID)
            continue;
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
            break;
        sock_close(fd);
        fd = SOCK_INVALID;
    }

    freeaddrinfo(res);
    if (fd == SOCK_INVALID)
        fprintf(stderr, "rion: connect(%s:%s) failed\n", host, port);
    return fd;
}

static int
send_all(sock_t fd, const void *buf, uint32_t len)
{
    const uint8_t *p = buf;
    uint32_t off = 0;

    while (off < len) {
        int chunk = (len - off > 65536U) ? 65536 : (int)(len - off);
        int n = (int)send(fd, (const char *)(p + off), chunk, 0);
        if (n < 0 && sock_interrupted())
            continue;
        if (n <= 0)
            return -1;
        off += (uint32_t)n;
    }
    return 0;
}

static int
recv_all(sock_t fd, void *buf, uint32_t len)
{
    uint8_t *p = buf;
    uint32_t off = 0;

    while (off < len) {
        int chunk = (len - off > 65536U) ? 65536 : (int)(len - off);
        int n = (int)recv(fd, (char *)(p + off), chunk, 0);
        if (n < 0 && sock_interrupted())
            continue;
        if (n <= 0)
            return -1;
        off += (uint32_t)n;
    }
    return 0;
}

static int
discard_bytes(sock_t fd, uint32_t len)
{
    uint8_t scratch[4096];

    while (len > 0) {
        uint32_t chunk = (len > sizeof(scratch)) ? (uint32_t)sizeof(scratch) : len;
        if (recv_all(fd, scratch, chunk) < 0)
            return -1;
        len -= chunk;
    }
    return 0;
}

static int
sock_readable(sock_t fd, int timeout_ms)
{
    fd_set rfds;
    struct timeval tv;

    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
#ifdef _WIN32
    return select(0, &rfds, NULL, NULL, &tv) > 0;
#else
    return select(fd + 1, &rfds, NULL, NULL, &tv) > 0;
#endif
}

static int
send_frame(sock_t fd, uint8_t type, const void *data, uint32_t len)
{
    uint8_t hdr[CHLORIDE_HDR_SIZE];

    if (len != 0 && data == NULL)
        return -1;
    hdr[0] = (uint8_t)CHLORIDE_MAGIC;
    hdr[1] = type;
    write_be16(hdr + 2, 0);
    write_be32(hdr + 4, len);

    if (send_all(fd, hdr, sizeof(hdr)) < 0)
        return -1;
    return (len > 0) ? send_all(fd, data, len) : 0;
}

static int
read_frame_hdr(sock_t fd, uint8_t *type_out, uint32_t *len_out, uint32_t max_len)
{
    uint8_t hdr[CHLORIDE_HDR_SIZE];
    uint8_t type;
    uint16_t flags;
    uint32_t len;

    int n;

    for (;;) {
        n = (int)recv(fd, (char *)hdr, 1, 0);
        if (n < 0 && sock_interrupted())
            continue;
        if (n == 0)
            return 1;
        if (n < 0) {
#ifdef _WIN32
            fprintf(stderr, "rion: recv header failed (WSA error %d)\n", WSAGetLastError());
#else
            perror("rion: recv header failed");
#endif
            return -1;
        }
        break;
    }
    if (recv_all(fd, hdr + 1, sizeof(hdr) - 1) < 0) {
        fprintf(stderr, "rion: truncated frame header\n");
        return -1;
    }

    if (hdr[0] != CHLORIDE_MAGIC) {
        fprintf(stderr, "rion: invalid magic byte: 0x%02X\n", hdr[0]);
        return -1;
    }

    type = hdr[1];
    if (type < FRAME_HANDSHAKE || type > FRAME_TERMINATE) {
        fprintf(stderr, "rion: unknown frame type: 0x%02X\n", type);
        return -1;
    }

    flags = read_be16(hdr + 2);
    if (flags != 0) {
        fprintf(stderr, "rion: nonzero reserved flags: 0x%04X\n", flags);
        return -1;
    }

    len = read_be32(hdr + 4);
    if (len > max_len) {
        fprintf(stderr, "rion: payload length %u exceeds limit %u\n", len, max_len);
        return -1;
    }

    *type_out = type;
    *len_out = len;
    return 0;
}

/*
 * Host ABI (Sec. 3.2): env.draw_frame(ptr, size)
 * Validates buffer bounds in linear memory before uploading pixels.
 */
m3ApiRawFunction(host_draw_frame)
{
    const uint32_t frame_bytes = CANVAS_WIDTH * CANVAS_HEIGHT * 4U;
    m3ApiGetArgMem(const void *, buffer_ptr)
    m3ApiGetArg(uint32_t, size)

    if (size != frame_bytes)
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);
    m3ApiCheckMem(buffer_ptr, frame_bytes);

    if (g_texture != NULL)
        SDL_UpdateTexture(g_texture, NULL, buffer_ptr,
            CANVAS_WIDTH * (int)sizeof(uint32_t));

    m3ApiSuccess();
}

static int
call_bounded(IM3Function fn, IM3Module mod, uint64_t freq, const char *name)
{
    uint64_t t0 = SDL_GetPerformanceCounter();
    M3Result res = m3_CallV(fn);
    size_t mem_size = 0;
    uint64_t elapsed_ms;

    if (res != NULL) {
        fprintf(stderr, "rion: %s() trap: %s\n", name, res);
        return -1;
    }
    (void)m3_GetMemory(mod, &mem_size, 0);
    if (mem_size > MAX_WASM_MEMORY) {
        fprintf(stderr, "rion: %s() exceeded memory limit (%u bytes)\n",
            name, (unsigned)MAX_WASM_MEMORY);
        return -1;
    }
    elapsed_ms = ((SDL_GetPerformanceCounter() - t0) * 1000ULL) / freq;
    if (elapsed_ms > MAX_FRAME_BUDGET_MS) {
        fprintf(stderr, "rion: %s() exceeded execution budget (%llu ms)\n",
            name, (unsigned long long)elapsed_ms);
        return -1;
    }
    return 0;
}

int
main(int argc, char *argv[])
{
    char host[256], port[16], path[256];
    const char *arg_target = NULL, *arg_port = NULL;
    uint32_t max_payload = DEFAULT_MAX_PAYLOAD;
    uint8_t *payload = NULL;
    uint32_t payload_len = 0;
    sock_t fd = SOCK_INVALID;
    IM3Environment env = NULL;
    IM3Runtime runtime = NULL;
    IM3Module module = NULL, loaded_mod = NULL;
    IM3Function init_fn = NULL, render_fn = NULL;
    M3Result res;
    SDL_Window *window = NULL;
    SDL_Event ev;
    uint64_t perf_freq;
    int i, sdl_ready = 0, running = 1, status = 1;

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "rion: WSAStartup failed\n");
        return 1;
    }
#else
    signal(SIGPIPE, SIG_IGN);
#endif

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            char *endp;
            unsigned long lim = strtoul(argv[++i], &endp, 10);
            if (*endp != '\0' || lim == 0 || lim > UINT32_MAX) {
                fprintf(stderr, "rion: invalid max payload limit\n");
                goto out;
            }
            max_payload = (uint32_t)lim;
        } else if (arg_target == NULL) {
            arg_target = argv[i];
        } else if (arg_port == NULL) {
            arg_port = argv[i];
        } else {
            fprintf(stderr, "usage: rion [-m max_bytes] [chloride://host[:port]/path | host [port]]\n");
            goto out;
        }
    }

    if (parse_endpoint(arg_target, arg_port,
        host, sizeof(host), port, sizeof(port), path, sizeof(path)) < 0) {
        fprintf(stderr, "rion: invalid endpoint\n");
        goto out;
    }

    printf("[rion] Connecting to chloride://%s:%s%s...\n", host, port, path);
    if ((fd = tcp_connect(host, port)) == SOCK_INVALID)
        goto out;


    if ((env = m3_NewEnvironment()) == NULL) {
        fprintf(stderr, "rion: failed to create Wasm3 environment\n");
        goto out;
    }

    for (;;) {
        uint8_t ftype;
        uint32_t flen;

        int hrc = read_frame_hdr(fd, &ftype, &flen, max_payload);
        if (hrc < 0)
            goto out;
        if (hrc > 0) {
            sock_close(fd);
            fd = SOCK_INVALID;
            break;
        }

        if (ftype == FRAME_HANDSHAKE) {
            if (discard_bytes(fd, flen) < 0)
                goto out;
        } else if (ftype == FRAME_CODE_STREAM) {
            uint8_t *next_buf;
            if (flen == 0)
                break;
            if (flen > max_payload - payload_len) {
                fprintf(stderr, "rion: cumulative CODE_STREAM exceeds limit %u\n", max_payload);
                goto out;
            }
            if ((next_buf = realloc(payload, payload_len + flen)) == NULL) {
                fprintf(stderr, "rion: out of memory\n");
                goto out;
            }
            payload = next_buf;
            if (recv_all(fd, payload + payload_len, flen) < 0) {
                fprintf(stderr, "rion: failed to read CODE_STREAM payload\n");
                goto out;
            }
            payload_len += flen;

            if (!sock_readable(fd, 25)) {
                IM3Module probe = NULL;
                M3Result pres = m3_ParseModule(env, &probe, payload, payload_len);
                if (probe != NULL)
                    m3_FreeModule(probe);
                if (pres != m3Err_wasmUnderrun)
                    break;
            }
        } else if (ftype == FRAME_ASSET_STREAM) {
            if (discard_bytes(fd, flen) < 0)
                goto out;
        } else if (ftype == FRAME_TERMINATE) {
            (void)discard_bytes(fd, flen);
            sock_close(fd);
            fd = SOCK_INVALID;
            break;
        } else {
            fprintf(stderr, "rion: unexpected frame type 0x%02X from server\n", ftype);
            goto out;
        }
    }

    if (fd != SOCK_INVALID) {
        (void)send_frame(fd, FRAME_TERMINATE, NULL, 0);
        sock_close(fd);
        fd = SOCK_INVALID;
    }

    if (payload_len == 0) {
        fprintf(stderr, "rion: received empty CODE_STREAM\n");
        goto out;
    }
    printf("[rion] Downloaded %u bytes of WASM bytecode.\n", payload_len);

    if ((runtime = m3_NewRuntime(env, 64 * 1024, NULL)) == NULL) {
        fprintf(stderr, "rion: failed to create Wasm3 runtime\n");
        goto out;
    }
    if ((res = m3_ParseModule(env, &module, payload, payload_len)) != NULL) {
        fprintf(stderr, "rion: m3_ParseModule: %s\n", res);
        goto out;
    }
    if ((res = m3_LoadModule(runtime, module)) != NULL) {
        fprintf(stderr, "rion: m3_LoadModule: %s\n", res);
        goto out;
    }

    {
        loaded_mod = module;
        size_t mem_size = 0;
        module = NULL; /* Owned by runtime after m3_LoadModule */

        (void)m3_GetMemory(loaded_mod, &mem_size, 0);
        if (mem_size > MAX_WASM_MEMORY) {
            fprintf(stderr, "rion: module memory (%lu bytes) exceeds limit (%u bytes)\n",
                (unsigned long)mem_size, (unsigned)MAX_WASM_MEMORY);
            goto out;
        }

        res = m3_LinkRawFunction(loaded_mod, "env", "draw_frame", "v(ii)", &host_draw_frame);
        if (res != NULL && res != m3Err_functionLookupFailed) {
            fprintf(stderr, "rion: m3_LinkRawFunction: %s\n", res);
            goto out;
        }
    }

    if ((res = m3_FindFunction(&render_fn, runtime, "render")) != NULL) {
        fprintf(stderr, "rion: missing 'render' export: %s\n", res);
        goto out;
    }

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "rion: SDL_Init: %s\n", SDL_GetError());
        goto out;
    }
    sdl_ready = 1;
    perf_freq = SDL_GetPerformanceFrequency();
    if (perf_freq == 0)
        perf_freq = 1000;

    window = SDL_CreateWindow("rion",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        CANVAS_WIDTH * 2, CANVAS_HEIGHT * 2,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (window == NULL) {
        fprintf(stderr, "rion: SDL_CreateWindow: %s\n", SDL_GetError());
        goto out;
    }
    if ((g_renderer = SDL_CreateRenderer(window, -1, 0)) == NULL) {
        fprintf(stderr, "rion: SDL_CreateRenderer: %s\n", SDL_GetError());
        goto out;
    }
    g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, CANVAS_WIDTH, CANVAS_HEIGHT);
    if (g_texture == NULL) {
        fprintf(stderr, "rion: SDL_CreateTexture: %s\n", SDL_GetError());
        goto out;
    }

    if (m3_FindFunction(&init_fn, runtime, "init") == NULL) {
        if (call_bounded(init_fn, loaded_mod, perf_freq, "init") < 0)
            goto out;
    }

    printf("[rion] Executing WASM application...\n");
    while (running) {
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT)
                running = 0;
        }
        if (call_bounded(render_fn, loaded_mod, perf_freq, "render") < 0)
            goto out;

        SDL_RenderClear(g_renderer);
        SDL_RenderCopy(g_renderer, g_texture, NULL, NULL);
        SDL_RenderPresent(g_renderer);
        SDL_Delay(16);
    }
    status = 0;

out:
    if (g_texture != NULL)
        SDL_DestroyTexture(g_texture);
    if (g_renderer != NULL)
        SDL_DestroyRenderer(g_renderer);
    if (window != NULL)
        SDL_DestroyWindow(window);
    if (sdl_ready)
        SDL_Quit();
    if (module != NULL)
        m3_FreeModule(module);
    if (runtime != NULL)
        m3_FreeRuntime(runtime);
    if (env != NULL)
        m3_FreeEnvironment(env);
    free(payload);
    if (fd != SOCK_INVALID) {
        (void)send_frame(fd, FRAME_TERMINATE, NULL, 0);
        sock_close(fd);
    }
#ifdef _WIN32
    WSACleanup();
#endif
    return status;
}
