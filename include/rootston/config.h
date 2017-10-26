#ifndef _ROOTSTON_CONFIG_H
#define _ROOTSTON_CONFIG_H
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_cursor.h>

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

struct binding_config {
	uint32_t modifiers;
	xkb_keysym_t *keysyms;
	size_t keysyms_len;
	char *command;
	struct wl_list link;
};

struct keyboard_config {
	char *name;
	uint32_t meta_key;
	char *rules;
	char *model;
	char *layout;
	char *variant;
	char *options;
	struct wl_list link;
};

struct roots_config {
	bool xwayland;
	// TODO: Multiple cursors, multiseat
	struct {
		char *mapped_output;
		struct wlr_box *mapped_box;
	} cursor;

	struct wl_list outputs;
	struct wl_list devices;
	struct wl_list bindings;
	struct wl_list keyboards;
	char *config_path;
	char *startup_cmd;
};

struct roots_config *parse_args(int argc, char *argv[]);

void roots_config_destroy(struct roots_config *config);

/**
 * Get configuration for the output. If the output is not configured, returns
 * NULL.
 */
struct output_config *config_get_output(struct roots_config *config,
	struct wlr_output *output);

/**
 * Get configuration for the device. If the device is not configured, returns
 * NULL.
 */
struct device_config *config_get_device(struct roots_config *config,
	struct wlr_input_device *device);

/**
 * Get configuration for the keyboard. If the keyboard is not configured,
 * returns NULL. A NULL device returns the default config for keyboards.
 */
struct keyboard_config *config_get_keyboard(struct roots_config *config,
	struct wlr_input_device *device);

#endif
