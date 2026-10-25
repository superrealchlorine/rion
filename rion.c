#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <SDL2/SDL.h>

#include "wasm3/source/wasm3.h"

#define DEFAULT_PORT 49130
#define MAGIC_BYTE 0x43
#define FRAME_CODE_STREAM 0x02
#define CANVAS_WIDTH 320
#define CANVAS_HEIGHT 240

struct __attribute__((__packed__)) chloride_header {
	uint8_t magic;
	uint8_t frame_type;
	uint16_t flags;
	uint32_t payload_len;
};

static SDL_Texture *g_texture = NULL;
static SDL_Renderer *g_renderer = NULL;

static int recv_all(int fd, void *buf, uint32_t len) {
	uint8_t *p = buf;
	uint32_t received = 0;

	while (received < len) {
		ssize_t n = recv(fd, p + received, len - received, 0);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			return -1;
		received += (uint32_t)n;
	}
	return 0;
}

/*
 * Host Import: env.draw_frame(buffer_ptr, size)
 * Translates WASM linear memory pointer into an SDL texture pixel upload
 */
m3ApiRawFunction(host_draw_frame) {
	m3ApiGetArgMem(const void *, buffer_ptr)
	m3ApiGetArg(uint32_t, size)

	if (buffer_ptr && g_texture && size <= (CANVAS_WIDTH * CANVAS_HEIGHT * 4)) {
		SDL_UpdateTexture(g_texture, NULL, buffer_ptr, CANVAS_WIDTH * sizeof(uint32_t));
	}

	m3ApiSuccess();
}

int main(int argc, char *argv[]) {
	const char *host = "127.0.0.1";
	int port = DEFAULT_PORT;
	int sock_fd;
	struct sockaddr_in server_addr;
	struct hostent *he;
	struct chloride_header hdr;
	uint32_t payload_len;
	uint8_t *wasm_payload = NULL;

	SDL_Window *window = NULL;
	SDL_Event event;
	int running = 1;

	/* Wasm3 engine handles */
	IM3Environment env = NULL;
	IM3Runtime runtime = NULL;
	IM3Module module = NULL;
	IM3Function render_func = NULL;
	M3Result result = m3Err_none;

	if (argc >= 2) {
		host = argv[1];
	}
	if (argc >= 3) {
		port = atoi(argv[2]);
	}

	printf("[rion] Connecting to %s:%d...\n", host, port);

	sock_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (sock_fd < 0) {
		perror("socket creation failed");
		return 1;
	}

	he = gethostbyname(host);
	if (!he) {
		fprintf(stderr, "Error resolving host: %s\n", host);
		close(sock_fd);
		return 1;
	}

	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	memcpy(&server_addr.sin_addr, he->h_addr_list[0], he->h_length);

	if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
		perror("connect failed");
		close(sock_fd);
		return 1;
	}

	printf("[rion] Connected. Reading Chloride frame header...\n");

	if (recv_all(sock_fd, &hdr, sizeof(hdr)) < 0) {
		fprintf(stderr, "Failed to read frame header\n");
		close(sock_fd);
		return 1;
	}

	if (hdr.magic != MAGIC_BYTE) {
		fprintf(stderr, "Invalid magic byte: 0x%02X\n", hdr.magic);
		close(sock_fd);
		return 1;
	}

	payload_len = ntohl(hdr.payload_len);
	if (hdr.frame_type != FRAME_CODE_STREAM) {
		fprintf(stderr, "Expected CODE_STREAM (0x02), got 0x%02X\n", hdr.frame_type);
		close(sock_fd);
		return 1;
	}

	wasm_payload = malloc(payload_len);
	if (!wasm_payload) {
		fprintf(stderr, "Memory allocation failed\n");
		close(sock_fd);
		return 1;
	}

	if (recv_all(sock_fd, wasm_payload, payload_len) < 0) {
		fprintf(stderr, "Failed to read WASM payload\n");
		free(wasm_payload);
		close(sock_fd);
		return 1;
	}

	printf("[rion] Downloaded %u bytes of WASM bytecode.\n", payload_len);
	close(sock_fd);

	/* Initialize Wasm3 Engine */
	env = m3_NewEnvironment();
	if (!env) {
		fprintf(stderr, "Failed to create Wasm3 environment\n");
		free(wasm_payload);
		return 1;
	}

	runtime = m3_NewRuntime(env, 64 * 1024, NULL); /* 64KB stack */
	if (!runtime) {
		fprintf(stderr, "Failed to create Wasm3 runtime\n");
		m3_FreeEnvironment(env);
		free(wasm_payload);
		return 1;
	}

	result = m3_ParseModule(env, &module, wasm_payload, payload_len);
	if (result) {
		fprintf(stderr, "m3_ParseModule error: %s\n", result);
		m3_FreeRuntime(runtime);
		m3_FreeEnvironment(env);
		free(wasm_payload);
		return 1;
	}

	result = m3_LoadModule(runtime, module);
	if (result) {
		fprintf(stderr, "m3_LoadModule error: %s\n", result);
		m3_FreeRuntime(runtime);
		m3_FreeEnvironment(env);
		free(wasm_payload);
		return 1;
	}

	/* Link host import env.draw_frame */
	m3_LinkRawFunction(module, "env", "draw_frame", "v(ii)", &host_draw_frame);

	/* Find exported entry point render() */
	result = m3_FindFunction(&render_func, runtime, "render");
	if (result) {
		fprintf(stderr, "Could not find 'render' function export in module: %s\n", result);
	}

	/* Initialize SDL Canvas Window */
	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
		m3_FreeRuntime(runtime);
		m3_FreeEnvironment(env);
		free(wasm_payload);
		return 1;
	}

	window = SDL_CreateWindow("rion",
							  SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
							  CANVAS_WIDTH * 2, CANVAS_HEIGHT * 2,
							  SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

	g_renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
	g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888,
								  SDL_TEXTUREACCESS_STREAMING, CANVAS_WIDTH, CANVAS_HEIGHT);

	printf("[rion] Executing WASM application...\n");

	while (running) {
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT) {
				running = 0;
			}
		}

		/* Invoke WASM render() function every frame */
		if (render_func) {
			m3_CallV(render_func);
		}

		SDL_RenderClear(g_renderer);
		SDL_RenderCopy(g_renderer, g_texture, NULL, NULL);
		SDL_RenderPresent(g_renderer);

		SDL_Delay(16);
	}

	SDL_DestroyTexture(g_texture);
	SDL_DestroyRenderer(g_renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();

	m3_FreeRuntime(runtime);
	m3_FreeEnvironment(env);
	free(wasm_payload);

	return 0;
}
