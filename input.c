#include "input.h"

#include <string.h>

static void
input_push(RionInput *input, const RionInputEvent *ev)
{
    if (input->count == INPUT_QUEUE_CAP) {
        input->head = (input->head + 1) % INPUT_QUEUE_CAP;
        input->count--;
    }
    input->queue[input->tail] = *ev;
    input->tail = (input->tail + 1) % INPUT_QUEUE_CAP;
    input->count++;
}

void
input_init(RionInput *input)
{
    memset(input, 0, sizeof(*input));
}

int
input_pop(RionInput *input, RionInputEvent *ev_out)
{
    if (input == NULL || input->count == 0)
        return 0;
    if (ev_out != NULL)
        *ev_out = input->queue[input->head];
    input->head = (input->head + 1) % INPUT_QUEUE_CAP;
    input->count--;
    return 1;
}

void
input_poll(RionInput *input)
{
    SDL_Event ev;

    if (input == NULL)
        return;

    while (SDL_PollEvent(&ev)) {
        RionInputEvent rev;
        memset(&rev, 0, sizeof(rev));

        switch (ev.type) {
        case SDL_QUIT:
            input->quit_requested = 1;
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            rev.type = (ev.type == SDL_KEYDOWN) ? 1 : 2;
            rev.code = (int32_t)ev.key.keysym.sym;
            rev.mods = (uint16_t)ev.key.keysym.mod;
            input_push(input, &rev);
            break;
        case SDL_MOUSEMOTION:
            rev.type = 3;
            rev.x = ev.motion.x;
            rev.y = ev.motion.y;
            input_push(input, &rev);
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            rev.type = (ev.type == SDL_MOUSEBUTTONDOWN) ? 4 : 5;
            rev.button = ev.button.button;
            rev.x = ev.button.x;
            rev.y = ev.button.y;
            input_push(input, &rev);
            break;
        default:
            break;
        }
    }
}

M3Result
input_link_wasm(IM3Module mod, RionState *state)
{
    (void)mod;
    (void)state;
    return NULL;
}
