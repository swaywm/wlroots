#ifndef _EXAMPLE_CONFIG_H
#define _EXAMPLE_CONFIG_H
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif
#include <wlr/types/wlr_output_layout.h>

struct output_config {
	char *name;
	enum wl_output_transform transform;
	int x, y;
	struct wl_list link;
};

struct device_config {
	char *name;
	char *mapped_output;
	struct wlr_box *mapped_box;
	struct wl_list link;
};

struct example_config {
	struct {
		char *mapped_output;
		struct wlr_box *mapped_box;
	} cursor;

	struct wl_list outputs;
	struct wl_list devices;
	char *config_path;
};

struct example_config *parse_args(int argc, char *argv[]);

void example_config_destroy(struct example_config *config);

struct wlr_output_layout *configure_layout(struct example_config *config,
		struct wl_list *outputs);
#endif
