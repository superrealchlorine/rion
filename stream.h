#ifndef RION_STREAM_H
#define RION_STREAM_H

#include "rion.h"

int stream_parse_endpoint(const char *arg, const char *port_override,
    char *host, size_t host_sz, char *port, size_t port_sz,
    char *path, size_t path_sz);

int stream_fetch_session(const char *host, const char *port,
    uint32_t max_payload, IM3Environment env, RionAssets *assets,
    uint8_t **wasm_out, uint32_t *wasm_len_out);

#endif /* RION_STREAM_H */
