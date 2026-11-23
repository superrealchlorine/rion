#ifndef RION_ASSETS_H
#define RION_ASSETS_H

#include "rion.h"

void assets_init(RionAssets *store);
void assets_free(RionAssets *store);
int assets_put(RionAssets *store, const char *name, const uint8_t *data, uint32_t size);
const RionAsset *assets_find(const RionAssets *store, const char *name);
int assets_ingest_frame(RionAssets *store, const uint8_t *payload, uint32_t len);

#endif /* RION_ASSETS_H */
