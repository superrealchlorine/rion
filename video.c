#include "video.h"

#include <stdio.h>
#include <string.h>

/*
 * Host ABI (Sec. 3.2): env.draw_frame(ptr, size)
 * Validates buffer bounds in linear memory before uploading pixels.
 */
m3ApiRawFunction(host_draw_frame)
{
    const uint32_t frame_bytes = CANVAS_WIDTH * CANVAS_HEIGHT * 4U;
    RionState *state = (RionState *)_ctx->userdata;
    m3ApiGetArgMem(const void *, buffer_ptr)
    m3ApiGetArg(uint32_t, size)

    if (size != frame_bytes)
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);
    m3ApiCheckMem(buffer_ptr, frame_bytes);

    if (state != NULL && state->video.framebuffer != NULL) {
        SDL_UpdateTexture(state->video.framebuffer, NULL, buffer_ptr,
            CANVAS_WIDTH * (int)sizeof(uint32_t));
    }

    m3ApiSuccess();
}

int
video_init(RionVideo *video)
{
    memset(video, 0, sizeof(*video));

    video->perf_freq = SDL_GetPerformanceFrequency();
    if (video->perf_freq == 0)
        video->perf_freq = 1000;

    video->window = SDL_CreateWindow("rion",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        CANVAS_WIDTH * 2, CANVAS_HEIGHT * 2,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (video->window == NULL) {
        fprintf(stderr, "rion: SDL_CreateWindow: %s\n", SDL_GetError());
        return -1;
    }

    video->renderer = SDL_CreateRenderer(video->window, -1, 0);
    if (video->renderer == NULL) {
        fprintf(stderr, "rion: SDL_CreateRenderer: %s\n", SDL_GetError());
        return -1;
    }

    video->framebuffer = SDL_CreateTexture(video->renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        CANVAS_WIDTH, CANVAS_HEIGHT);
    if (video->framebuffer == NULL) {
        fprintf(stderr, "rion: SDL_CreateTexture: %s\n", SDL_GetError());
        return -1;
    }

    return 0;
}

void
video_present(RionVideo *video)
{
    if (video == NULL || video->renderer == NULL)
        return;
    SDL_RenderClear(video->renderer);
    if (video->framebuffer != NULL)
        SDL_RenderCopy(video->renderer, video->framebuffer, NULL, NULL);
    SDL_RenderPresent(video->renderer);
}

void
video_destroy(RionVideo *video)
{
    if (video == NULL)
        return;
    if (video->framebuffer != NULL)
        SDL_DestroyTexture(video->framebuffer);
    if (video->renderer != NULL)
        SDL_DestroyRenderer(video->renderer);
    if (video->window != NULL)
        SDL_DestroyWindow(video->window);
    memset(video, 0, sizeof(*video));
}

M3Result
video_link_wasm(IM3Module mod, RionState *state)
{
    M3Result res = m3_LinkRawFunctionEx(mod, "env", "draw_frame",
        "v(ii)", &host_draw_frame, state);
    if (res == m3Err_functionLookupFailed)
        return NULL;
    return res;
}
