#ifndef RION_INPUT_H
#define RION_INPUT_H

#include "rion.h"

void input_init(RionInput *input);
void input_poll(RionInput *input);
int input_pop(RionInput *input, RionInputEvent *ev_out);
M3Result input_link_wasm(IM3Module mod, RionState *state);

#endif /* RION_INPUT_H */
