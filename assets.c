#include "assets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *
dup_cstr(const char *s)
{
    size_t n = strlen(s) + 1;
    char *copy = malloc(n);
    if (copy != NULL)
        memcpy(copy, s, n);
    return copy;
}

void
assets_init(RionAssets *store)
{
    memset(store, 0, sizeof(*store));
}

void
assets_free(RionAssets *store)
{
    size_t i;

    if (store == NULL)
        return;
    for (i = 0; i < store->count; i++) {
        free(store->items[i].name);
        free(store->items[i].data);
    }
    free(store->items);
    memset(store, 0, sizeof(*store));
}

const RionAsset *
assets_find(const RionAssets *store, const char *name)
{
    size_t i;

    if (store == NULL || name == NULL)
        return NULL;
    for (i = 0; i < store->count; i++) {
        if (strcmp(store->items[i].name, name) == 0)
            return &store->items[i];
    }
    return NULL;
}

int
assets_put(RionAssets *store, const char *name, const uint8_t *data, uint32_t size)
{
    uint8_t *buf = NULL;
    size_t i;

    if (store == NULL || name == NULL || (size > 0 && data == NULL))
        return -1;

    if (size > 0) {
        buf = malloc(size);
        if (buf == NULL)
            return -1;
        memcpy(buf, data, size);
    }

    for (i = 0; i < store->count; i++) {
        if (strcmp(store->items[i].name, name) == 0) {
            free(store->items[i].data);
            store->items[i].data = buf;
            store->items[i].size = size;
            return 0;
        }
    }

    if (store->count == store->cap) {
        size_t next_cap = (store->cap == 0) ? 8 : store->cap * 2;
        RionAsset *next = realloc(store->items, next_cap * sizeof(*next));
        if (next == NULL) {
            free(buf);
            return -1;
        }
        store->items = next;
        store->cap = next_cap;
    }

    store->items[store->count].name = dup_cstr(name);
    if (store->items[store->count].name == NULL) {
        free(buf);
        return -1;
    }
    store->items[store->count].data = buf;
    store->items[store->count].size = size;
    store->count++;
    return 0;
}

int
assets_ingest_frame(RionAssets *store, const uint8_t *payload, uint32_t len)
{
    char name_buf[256];

    if (len >= 2) {
        uint16_t name_len = ((uint16_t)payload[0] << 8) | (uint16_t)payload[1];
        if (name_len > 0 && name_len < sizeof(name_buf) &&
            (uint32_t)name_len + 2U <= len) {
            memcpy(name_buf, payload + 2, name_len);
            name_buf[name_len] = '\0';
            return assets_put(store, name_buf,
                payload + 2 + name_len, len - 2U - name_len);
        }
    }

    snprintf(name_buf, sizeof(name_buf), "asset_%lu", (unsigned long)store->count);
    return assets_put(store, name_buf, payload, len);
}
