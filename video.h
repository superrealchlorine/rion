#ifndef RION_VIDEO_H
#define RION_VIDEO_H

#include "rion.h"

int video_init(RionVideo *video);
void video_present(RionVideo *video);
void video_destroy(RionVideo *video);
M3Result video_link_wasm(IM3Module mod, RionState *state);

#endif /* RION_VIDEO_H */
