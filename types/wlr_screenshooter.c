#include <assert.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/types/wlr_screenshooter.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>
#include "screenshooter-protocol.h"

static void screenshooter_shoot(struct wl_client *client,
		struct wl_resource *_screenshooter, uint32_t id,
		struct wl_resource *_output, struct wl_resource *_buffer) {
	struct wlr_screenshot *screenshot;
	if (!(screenshot = calloc(1, sizeof(struct wlr_screenshot)))) {
		return;
	}
	screenshot->output = _output;
	screenshot->resource = wl_resource_create(client,
		&orbital_screenshot_interface, wl_resource_get_version(_screenshooter), id);
	wlr_log(L_DEBUG, "new screenshot %p (res %p)", screenshot, screenshot->resource);
	wl_resource_set_implementation(screenshot->resource, NULL, screenshot, NULL);
	// TODO: orbital_screenshot_send_done(screenshot->resource);
}

static struct orbital_screenshooter_interface screenshooter_impl = {
	.shoot = screenshooter_shoot,
};

static void screenshooter_bind(struct wl_client *wl_client,
		void *_screenshooter, uint32_t version, uint32_t id) {
	struct wlr_screenshooter *screenshooter = _screenshooter;
	assert(wl_client && screenshooter);
	if (version > 1) {
		wlr_log(L_ERROR, "Client requested unsupported screenshooter version, disconnecting");
		wl_client_destroy(wl_client);
		return;
	}
	struct wl_resource *wl_resource = wl_resource_create(wl_client,
		&orbital_screenshooter_interface, version, id);
	wl_resource_set_implementation(wl_resource, &screenshooter_impl,
		screenshooter, NULL);
}

struct wlr_screenshooter *wlr_screenshooter_create(struct wl_display *display) {
	struct wlr_screenshooter *screenshooter =
		calloc(1, sizeof(struct wlr_screenshooter));
	if (!screenshooter) {
		return NULL;
	}
	struct wl_global *wl_global = wl_global_create(display,
		&orbital_screenshooter_interface, 1, screenshooter, screenshooter_bind);
	if (!wl_global) {
		free(screenshooter);
		return NULL;
	}
	screenshooter->wl_global = wl_global;
	return screenshooter;
}

void wlr_screenshooter_destroy(struct wlr_screenshooter *screenshooter) {
	if (!screenshooter) {
		return;
	}
	// TODO: this segfault (wl_display->registry_resource_list is not init)
	// wl_global_destroy(screenshooter->wl_global);
	free(screenshooter);
}
