#include <getopt.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <wlr/util/log.h>
#include "gamma-control-client-protocol.h"

static struct wl_output *output;
static struct gamma_control_manager *gamma_control_manager;
static struct gamma_control *control;

static void handle_gamma_size(void *data, struct gamma_control *gamma_control,
		uint32_t size) {
	fprintf(stdout, "gamma size %d\n", size);
}

static void handle_gamma(void *data,
	struct gamma_control *gamma_control,
	struct wl_array *red,
	struct wl_array *green,
	struct wl_array *blue) {
	uint16_t *r = red->data;
	uint16_t *b = blue->data;
	uint16_t *g = green->data;
	fprintf(stdout, "Reading Gamma\n"
		"Num|   red| green|  blue|\n"
		"---+------+------+------+\n");
	for (uint32_t i = 0; i < red->size/sizeof(uint16_t); i++) {
		fprintf(stdout, "%3i| %5d| %5d| %5d|\n", i, r[i], g[i], b[i]);
	}
}

static const struct gamma_control_listener gamma_control_listener = {
	.gamma_size = handle_gamma_size,
	.gamma = handle_gamma,
};

static void handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	fprintf(stdout, "interface found: %s\n", interface);
	if (strcmp(interface, gamma_control_manager_interface.name) == 0) {
		gamma_control_manager = wl_registry_bind(registry, name, &gamma_control_manager_interface, 1);
	} else if (!strcmp (interface, "wl_output")) {
		output = wl_registry_bind (registry, name, &wl_output_interface, 1);
	}
}

static void handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
	//TODO
}

static const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = handle_global_remove,
};


int main(int argc, char *argv[]) {
	wlr_log_init(L_DEBUG, NULL);

	struct wl_display *display = wl_display_connect(NULL);
	if (display == NULL) {
		fprintf(stderr, "failed to create display\n");
		return -1;
	}

	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_dispatch(display);
	wl_display_roundtrip(display);

	if (gamma_control_manager == NULL) {
		fprintf(stderr, "display doesn't support gamma control protocol\n");
		return -1;
	}
	if (output == NULL) {
		fprintf(stderr, "no output found\n");
		return -1;
	}

    control = gamma_control_manager_get_gamma_control (gamma_control_manager,
		output);
	gamma_control_add_listener(control, &gamma_control_listener, NULL);

	while (wl_display_dispatch(display) != -1) {
		// This space intentionally left blank
	}

	gamma_control_destroy (control);
	wl_display_disconnect(display);
	return 0;
}
