#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#endif

#include "assets.h"
#include "input.h"
#include "rion.h"
#include "stream.h"
#include "video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    RionState state;
    IM3Environment env = NULL;
    IM3Runtime runtime = NULL;
    IM3Module module = NULL, loaded_mod = NULL;
    IM3Function init_fn = NULL, render_fn = NULL;
    M3Result res;
    int i, sdl_ready = 0, status = 1;

    memset(&state, 0, sizeof(state));
    assets_init(&state.assets);
    input_init(&state.input);

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

    if (stream_parse_endpoint(arg_target, arg_port,
        host, sizeof(host), port, sizeof(port), path, sizeof(path)) < 0) {
        fprintf(stderr, "rion: invalid endpoint\n");
        goto out;
    }

    if ((env = m3_NewEnvironment()) == NULL) {
        fprintf(stderr, "rion: failed to create Wasm3 environment\n");
        goto out;
    }

    printf("[rion] Connecting to chloride://%s:%s%s...\n", host, port, path);
    if (stream_fetch_session(host, port, max_payload, env,
        &state.assets, &payload, &payload_len) < 0) {
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
        size_t mem_size = 0;
        loaded_mod = module;
        module = NULL; /* Owned by runtime after m3_LoadModule */

        (void)m3_GetMemory(loaded_mod, &mem_size, 0);
        if (mem_size > MAX_WASM_MEMORY) {
            fprintf(stderr, "rion: module memory (%lu bytes) exceeds limit (%u bytes)\n",
                (unsigned long)mem_size, (unsigned)MAX_WASM_MEMORY);
            goto out;
        }

        if ((res = video_link_wasm(loaded_mod, &state)) != NULL ||
            (res = input_link_wasm(loaded_mod, &state)) != NULL) {
            fprintf(stderr, "rion: m3_LinkRawFunctionEx: %s\n", res);
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

    if (video_init(&state.video) < 0)
        goto out;

    if (m3_FindFunction(&init_fn, runtime, "init") == NULL) {
        if (call_bounded(init_fn, loaded_mod, state.video.perf_freq, "init") < 0)
            goto out;
    }

    printf("[rion] Executing WASM application...\n");
    while (!state.input.quit_requested) {
        input_poll(&state.input);
        if (call_bounded(render_fn, loaded_mod, state.video.perf_freq, "render") < 0)
            goto out;

        video_present(&state.video);
        SDL_Delay(16);
    }
    status = 0;

out:
    video_destroy(&state.video);
    if (sdl_ready)
        SDL_Quit();
    if (module != NULL)
        m3_FreeModule(module);
    if (runtime != NULL)
        m3_FreeRuntime(runtime);
    if (env != NULL)
        m3_FreeEnvironment(env);
    assets_free(&state.assets);
    free(payload);
#ifdef _WIN32
    WSACleanup();
#endif
    return status;
}
