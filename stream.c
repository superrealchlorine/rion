#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif

#include "assets.h"
#include "stream.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int
stream_parse_endpoint(const char *arg, const char *port_override,
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

int
stream_fetch_session(const char *host, const char *port,
    uint32_t max_payload, IM3Environment env, RionAssets *assets,
    uint8_t **wasm_out, uint32_t *wasm_len_out)
{
    sock_t fd;
    uint8_t *payload = NULL;
    uint32_t payload_len = 0;

    *wasm_out = NULL;
    *wasm_len_out = 0;

    if ((fd = tcp_connect(host, port)) == SOCK_INVALID)
        return -1;

    for (;;) {
        uint8_t ftype;
        uint32_t flen;
        int hrc = read_frame_hdr(fd, &ftype, &flen, max_payload);

        if (hrc < 0)
            goto fail;
        if (hrc > 0) {
            sock_close(fd);
            fd = SOCK_INVALID;
            break;
        }

        if (ftype == FRAME_HANDSHAKE) {
            if (discard_bytes(fd, flen) < 0)
                goto fail;
        } else if (ftype == FRAME_CODE_STREAM) {
            uint8_t *next_buf;
            if (flen == 0)
                break;
            if (flen > max_payload - payload_len) {
                fprintf(stderr, "rion: cumulative CODE_STREAM exceeds limit %u\n", max_payload);
                goto fail;
            }
            if ((next_buf = realloc(payload, payload_len + flen)) == NULL) {
                fprintf(stderr, "rion: out of memory\n");
                goto fail;
            }
            payload = next_buf;
            if (recv_all(fd, payload + payload_len, flen) < 0) {
                fprintf(stderr, "rion: failed to read CODE_STREAM payload\n");
                goto fail;
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
            uint8_t *abuf = NULL;
            if (flen > 0) {
                abuf = malloc(flen);
                if (abuf == NULL) {
                    fprintf(stderr, "rion: out of memory reading ASSET_STREAM\n");
                    goto fail;
                }
                if (recv_all(fd, abuf, flen) < 0) {
                    free(abuf);
                    goto fail;
                }
            }
            if (assets_ingest_frame(assets, abuf, flen) < 0) {
                free(abuf);
                fprintf(stderr, "rion: failed to store asset\n");
                goto fail;
            }
            free(abuf);
        } else if (ftype == FRAME_TERMINATE) {
            (void)discard_bytes(fd, flen);
            sock_close(fd);
            fd = SOCK_INVALID;
            break;
        } else {
            fprintf(stderr, "rion: unexpected frame type 0x%02X from server\n", ftype);
            goto fail;
        }
    }

    if (fd != SOCK_INVALID) {
        (void)send_frame(fd, FRAME_TERMINATE, NULL, 0);
        sock_close(fd);
    }

    if (payload_len == 0) {
        fprintf(stderr, "rion: received empty CODE_STREAM\n");
        free(payload);
        return -1;
    }

    *wasm_out = payload;
    *wasm_len_out = payload_len;
    return 0;

fail:
    free(payload);
    if (fd != SOCK_INVALID) {
        (void)send_frame(fd, FRAME_TERMINATE, NULL, 0);
        sock_close(fd);
    }
    return -1;
}
