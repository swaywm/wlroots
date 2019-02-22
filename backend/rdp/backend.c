#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/render/egl.h>
#include <wlr/render/gles2.h>
#include <wlr/util/log.h>
#include "backend/rdp.h"
#include "glapi.h"
#include "util/signal.h"

struct wlr_rdp_backend *rdp_backend_from_backend(
		struct wlr_backend *wlr_backend) {
	assert(wlr_backend_is_rdp(wlr_backend));
	return (struct wlr_rdp_backend *)wlr_backend;
}

static bool backend_start(struct wlr_backend *wlr_backend) {
	struct wlr_rdp_backend *backend =
		rdp_backend_from_backend(wlr_backend);
	assert(backend->listener == NULL);
	wlr_log(WLR_INFO, "Starting RDP backend");
	if (!rdp_configure_listener(backend)) {
		return false;
	}
	return true;
}

void wlr_rdp_backend_set_address(struct wlr_backend *wlr_backend,
		const char *address) {
	struct wlr_rdp_backend *backend =
		rdp_backend_from_backend(wlr_backend);
	assert(backend->listener == NULL);
	backend->address = strdup(address);
}

void wlr_rdp_backend_set_port(struct wlr_backend *wlr_backend, int port) {
	struct wlr_rdp_backend *backend =
		rdp_backend_from_backend(wlr_backend);
	assert(backend->listener == NULL);
	backend->port = port;
}

static void backend_destroy(struct wlr_backend *wlr_backend) {
	struct wlr_rdp_backend *backend =
		rdp_backend_from_backend(wlr_backend);
	if (!wlr_backend) {
		return;
	}

	wl_list_remove(&backend->display_destroy.link);

	struct wlr_rdp_peer_context *client;
	wl_list_for_each(client, &backend->clients, link) {
		freerdp_peer_context_free(client->peer);
		freerdp_peer_free(client->peer);
	}

	wlr_signal_emit_safe(&wlr_backend->events.destroy, backend);

	wlr_renderer_destroy(backend->renderer);
	wlr_egl_finish(&backend->egl);
	free(backend->address);
	free(backend);
}

static struct wlr_renderer *backend_get_renderer(
		struct wlr_backend *wlr_backend) {
	struct wlr_rdp_backend *backend =
		rdp_backend_from_backend(wlr_backend);
	return backend->renderer;
}

static const struct wlr_backend_impl backend_impl = {
	.start = backend_start,
	.destroy = backend_destroy,
	.get_renderer = backend_get_renderer,
};

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_rdp_backend *backend =
		wl_container_of(listener, backend, display_destroy);
	backend_destroy(&backend->backend);
}

struct wlr_backend *wlr_rdp_backend_create(struct wl_display *display,
		wlr_renderer_create_func_t create_renderer_func,
		const char *tls_cert_path, const char *tls_key_path) {
	wlr_log(WLR_INFO, "Creating RDP backend");

	struct wlr_rdp_backend *backend =
		calloc(1, sizeof(struct wlr_rdp_backend));
	if (!backend) {
		wlr_log(WLR_ERROR, "Failed to allocate wlr_rdp_backend");
		return NULL;
	}
	wlr_backend_init(&backend->backend, &backend_impl);
	backend->display = display;
	backend->tls_cert_path = tls_cert_path;
	backend->tls_key_path = tls_key_path;
	backend->address = strdup("127.0.0.1");
	backend->port = 3389;
	wl_list_init(&backend->clients);

	static const EGLint config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_ALPHA_SIZE, 0,
		EGL_BLUE_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_RED_SIZE, 1,
		EGL_NONE,
	};

	if (!create_renderer_func) {
		create_renderer_func = wlr_renderer_autocreate;
	}

	backend->renderer = create_renderer_func(&backend->egl,
		EGL_PLATFORM_SURFACELESS_MESA, NULL, (EGLint*)config_attribs, 0);
	if (!backend->renderer) {
		wlr_log(WLR_ERROR, "Failed to create renderer");
		free(backend);
		return NULL;
	}

	backend->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &backend->display_destroy);

	return &backend->backend;
}

bool wlr_backend_is_rdp(struct wlr_backend *backend) {
	return backend->impl == &backend_impl;
}
