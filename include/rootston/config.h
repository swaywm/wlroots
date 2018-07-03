#ifndef ROOTSTON_CONFIG_H
#define ROOTSTON_CONFIG_H

#include <xf86drmMode.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_output_layout.h>

#define ROOTS_CONFIG_DEFAULT_SEAT_NAME "seat0"

struct roots_output_mode_config {
	drmModeModeInfo info;
	struct wl_list link;
};

struct roots_output_config {
	char *name;
	bool enable;
	enum wl_output_transform transform;
	int x, y;
	float scale;
	struct wl_list link;
	struct {
		int width, height;
		float refresh_rate;
	} mode;
	struct wl_list modes;
};

struct roots_device_config {
	char *name;
	char *seat;
	char *mapped_output;
	bool tap_enabled;
	struct wlr_box *mapped_box;
	struct wl_list link;
};

struct roots_binding_config {
	uint32_t modifiers;
	xkb_keysym_t *keysyms;
	size_t keysyms_len;
	char *command;
	struct wl_list link;
};

struct roots_keyboard_config {
	char *name;
	char *seat;
	uint32_t meta_key;
	char *rules;
	char *model;
	char *layout;
	char *variant;
	char *options;
	int repeat_rate, repeat_delay;
	struct wl_list link;
};

struct roots_cursor_config {
	char *seat;
	char *mapped_output;
	struct wlr_box *mapped_box;
	char *theme;
	char *default_image;
	struct wl_list link;
};

struct roots_config {
	bool xwayland;
	bool xwayland_lazy;

	struct wl_list outputs;
	struct wl_list devices;
	struct wl_list bindings;
	struct wl_list keyboards;
	struct wl_list cursors;

	char *config_path;
	char *startup_cmd;
	bool debug_damage_tracking;
};

/**
 * Create a roots config from the given command line arguments. Command line
 * arguments can specify the location of the config file. If it is not
 * specified, the default location will be used.
 */
struct roots_config *roots_config_create_from_args(int argc, char *argv[]);

/**
 * Destroy the config and free its resources.
 */
void roots_config_destroy(struct roots_config *config);

/**
 * Get configuration for the output. If the output is not configured, returns
 * NULL.
 */
struct roots_output_config *roots_config_get_output(struct roots_config *config,
	struct wlr_output *output);

/**
 * Get configuration for the device. If the device is not configured, returns
 * NULL.
 */
struct roots_device_config *roots_config_get_device(struct roots_config *config,
	struct wlr_input_device *device);

/**
 * Get configuration for the keyboard. If the keyboard is not configured,
 * returns NULL. A NULL device returns the default config for keyboards.
 */
struct roots_keyboard_config *roots_config_get_keyboard(
	struct roots_config *config, struct wlr_input_device *device);

/**
 * Get configuration for the cursor. If the cursor is not configured, returns
 * NULL. A NULL seat_name returns the default config for cursors.
 */
struct roots_cursor_config *roots_config_get_cursor(struct roots_config *config,
	const char *seat_name);

#endif
