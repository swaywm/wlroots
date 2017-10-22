#include <assert.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/types/wlr_gamma_control.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>
#include "gamma-control-protocol.h"

static void resource_destroy(struct wl_client *client, struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void gamma_control_destroy(struct wl_resource *resource) {
	struct wlr_gamma_control *gamma_control = wl_resource_get_user_data(resource);
	free(gamma_control);
}

static void gamma_control_set_gamma(struct wl_client *client,
		struct wl_resource *_gamma_control, struct wl_array *red,
		struct wl_array *green, struct wl_array *blue) {
	if (red->size != green->size || red->size != blue->size) {
		wl_resource_post_error(_gamma_control, GAMMA_CONTROL_ERROR_INVALID_GAMMA,
			"The gamma ramps don't have the same size");
		return;
	}
	uint16_t *r = (uint16_t *)red->data;
	uint16_t *g = (uint16_t *)green->data;
	uint16_t *b = (uint16_t *)blue->data;
	struct wlr_gamma_control *gamma_control = wl_resource_get_user_data(_gamma_control);
	struct wlr_output *output = wl_resource_get_user_data(gamma_control->output);
	wlr_output_set_gamma(output, red->size / sizeof(uint16_t), r, g, b);
}

static void gamma_control_reset_gamma(struct wl_client *client,
		struct wl_resource *_gamma_control) {
	// TODO
}

static const struct gamma_control_interface gamma_control_implementation = {
	.destroy = resource_destroy,
	.set_gamma = gamma_control_set_gamma,
	.reset_gamma = gamma_control_reset_gamma,
};

static void gamma_control_manager_get_gamma_control(struct wl_client *client,
		struct wl_resource *_gamma_control_manager, uint32_t id,
		struct wl_resource *_output) {
	//struct wlr_gamma_control_manager *gamma_control_manager =
	//	wl_resource_get_user_data(_gamma_control_manager);
	struct wlr_output *output = wl_resource_get_user_data(_output);
	struct wlr_gamma_control *gamma_control;
	if (!(gamma_control = calloc(1, sizeof(struct wlr_gamma_control)))) {
		return;
	}
	gamma_control->output = _output;
	gamma_control->resource = wl_resource_create(client,
		&gamma_control_interface, wl_resource_get_version(_gamma_control_manager), id);
	wlr_log(L_DEBUG, "new gamma_control %p (res %p)", gamma_control, gamma_control->resource);
	wl_resource_set_implementation(gamma_control->resource,
		&gamma_control_implementation, gamma_control, gamma_control_destroy);
	gamma_control_send_gamma_size(gamma_control->resource, wlr_output_get_gamma_size(output));
}

static struct gamma_control_manager_interface gamma_control_manager_impl = {
	.get_gamma_control = gamma_control_manager_get_gamma_control,
};

static void gamma_control_manager_bind(struct wl_client *wl_client,
		void *_gamma_control_manager, uint32_t version, uint32_t id) {
	struct wlr_gamma_control_manager *gamma_control_manager = _gamma_control_manager;
	assert(wl_client && gamma_control_manager);

	struct wl_resource *wl_resource = wl_resource_create(
		wl_client, &gamma_control_manager_interface, version, id);
	wl_resource_set_implementation(wl_resource, &gamma_control_manager_impl,
		gamma_control_manager, NULL);
}

struct wlr_gamma_control_manager *wlr_gamma_control_manager_create(struct wl_display *display) {
	struct wlr_gamma_control_manager *gamma_control_manager =
		calloc(1, sizeof(struct wlr_gamma_control_manager));
	if (!gamma_control_manager) {
		return NULL;
	}
	struct wl_global *wl_global = wl_global_create(display,
		&gamma_control_manager_interface, 1, gamma_control_manager, gamma_control_manager_bind);
	if (!wl_global) {
		free(gamma_control_manager);
		return NULL;
	}
	gamma_control_manager->wl_global = wl_global;
	return gamma_control_manager;
}

void wlr_gamma_control_manager_destroy(struct wlr_gamma_control_manager *gamma_control_manager) {
	if (!gamma_control_manager) {
		return;
	}
	// TODO: this segfault (wl_display->registry_resource_list is not init)
	// wl_global_destroy(gamma_control_manager->wl_global);
	free(gamma_control_manager);
}
