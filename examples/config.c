#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdlib.h>
#include <limits.h>
#include <getopt.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>
#include <wlr/util/log.h>
#include <wlr/types/wlr_box.h>
#include "shared.h"
#include "config.h"
#include "ini.h"

static void usage(const char *name, int ret) {
	fprintf(stderr,
		"usage: %s [-C <FILE>]\n"
		"\n"
		" -C <FILE>      Path to the configuration file\n"
		"                (default: wlr-example.ini).\n"
		"                See `rootston/rootston.ini.example` for config\n"
		"                file documentation.\n", name);

	exit(ret);
}

static struct wlr_box *parse_geometry(const char *str) {
	// format: {width}x{height}+{x}+{y}
	if (strlen(str) > 255) {
		wlr_log(L_ERROR, "cannot parse geometry string, too long");
		return NULL;
	}

	char *buf = strdup(str);
	struct wlr_box *box = calloc(1, sizeof(struct wlr_box));

	bool has_width = false;
	bool has_height = false;
	bool has_x = false;
	bool has_y = false;

	char *pch = strtok(buf, "x+");
	while (pch != NULL) {
		errno = 0;
		char *endptr;
		long val = strtol(pch, &endptr, 0);

		if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN)) ||
				(errno != 0 && val == 0)) {
			goto invalid_input;
		}

		if (endptr == pch) {
			goto invalid_input;
		}

		if (!has_width) {
			box->width = val;
			has_width = true;
		} else if (!has_height) {
			box->height = val;
			has_height = true;
		} else if (!has_x) {
			box->x = val;
			has_x = true;
		} else if (!has_y) {
			box->y = val;
			has_y = true;
		} else {
			break;
		}
		pch = strtok(NULL, "x+");
	}

	if (!has_width || !has_height) {
		goto invalid_input;
	}

	free(buf);
	return box;

invalid_input:
	wlr_log(L_ERROR, "could not parse geometry string: %s", str);
	free(buf);
	free(box);
	return NULL;
}

static const char *output_prefix = "output:";
static const char *device_prefix = "device:";

static int config_ini_handler(void *user, const char *section, const char *name,
		const char *value) {
	struct example_config *config = user;
	if (strncmp(output_prefix, section, strlen(output_prefix)) == 0) {
		const char *output_name = section + strlen(output_prefix);
		struct output_config *oc;
		bool found = false;

		wl_list_for_each(oc, &config->outputs, link) {
			if (strcmp(oc->name, output_name) == 0) {
				found = true;
				break;
			}
		}

		if (!found) {
			oc = calloc(1, sizeof(struct output_config));
			oc->name = strdup(output_name);
			oc->transform = WL_OUTPUT_TRANSFORM_NORMAL;
			wl_list_insert(&config->outputs, &oc->link);
		}

		if (strcmp(name, "x") == 0) {
			oc->x = strtol(value, NULL, 10);
		} else if (strcmp(name, "y") == 0) {
			oc->y = strtol(value, NULL, 10);
		} else if (strcmp(name, "rotate") == 0) {
			if (strcmp(value, "90") == 0) {
				oc->transform = WL_OUTPUT_TRANSFORM_90;
			} else if (strcmp(value, "180") == 0) {
				oc->transform = WL_OUTPUT_TRANSFORM_180;
			} else if (strcmp(value, "270") == 0) {
				oc->transform = WL_OUTPUT_TRANSFORM_270;
			} else if (strcmp(value, "flipped") == 0) {
				oc->transform = WL_OUTPUT_TRANSFORM_FLIPPED;
			} else if (strcmp(value, "flipped-90") == 0) {
				oc->transform = WL_OUTPUT_TRANSFORM_FLIPPED_90;
			} else if (strcmp(value, "flipped-180") == 0) {
				oc->transform = WL_OUTPUT_TRANSFORM_FLIPPED_180;
			} else if (strcmp(value, "flipped-270") == 0) {
				oc->transform = WL_OUTPUT_TRANSFORM_FLIPPED_270;
			} else {
				wlr_log(L_ERROR, "got unknown transform value: %s", value);
			}
		}
	} else if (strcmp(section, "cursor") == 0) {
		if (strcmp(name, "map-to-output") == 0) {
			free(config->cursor.mapped_output);
			config->cursor.mapped_output = strdup(value);
		} else if (strcmp(name, "geometry") == 0) {
			free(config->cursor.mapped_box);
			config->cursor.mapped_box = parse_geometry(value);
		} else {
			wlr_log(L_ERROR, "got unknown cursor config: %s", name);
		}
	} else if (strncmp(device_prefix, section, strlen(device_prefix)) == 0) {
		const char *device_name = section + strlen(device_prefix);
		struct device_config *dc;
		bool found = false;

		wl_list_for_each(dc, &config->devices, link) {
			if (strcmp(dc->name, device_name) == 0) {
				found = true;
				break;
			}
		}

		if (!found) {
			dc = calloc(1, sizeof(struct device_config));
			dc->name = strdup(device_name);
			wl_list_insert(&config->devices, &dc->link);
		}

		if (strcmp(name, "map-to-output") == 0) {
			free(dc->mapped_output);
			dc->mapped_output = strdup(value);
		} else if (strcmp(name, "geometry") == 0) {
			free(dc->mapped_box);
			dc->mapped_box = parse_geometry(value);
		} else {
			wlr_log(L_ERROR, "got unknown device config: %s", name);
		}
	} else {
		wlr_log(L_ERROR, "got unknown config section: %s", section);
	}

	return 1;
}

struct example_config *parse_args(int argc, char *argv[]) {
	struct example_config *config = calloc(1, sizeof(struct example_config));
	wl_list_init(&config->outputs);
	wl_list_init(&config->devices);

	int c;
	while ((c = getopt(argc, argv, "C:h")) != -1) {
		switch (c) {
		case 'C':
			config->config_path = strdup(optarg);
			break;
		case 'h':
		case '?':
			usage(argv[0], c != 'h');
		}
	}

	if (!config->config_path) {
		// get the config path from the current directory
		char cwd[MAXPATHLEN];
		if (getcwd(cwd, sizeof(cwd)) != NULL) {
			char buf[MAXPATHLEN];
			snprintf(buf, MAXPATHLEN, "%s/%s", cwd, "wlr-example.ini");
			config->config_path = strdup(buf);
		} else {
			wlr_log(L_ERROR, "could not get cwd");
			exit(1);
		}
	}

	int result = ini_parse(config->config_path, config_ini_handler, config);

	if (result == -1) {
		wlr_log(L_DEBUG, "No config file found. Using empty config.");
	} else if (result == -2) {
		wlr_log(L_ERROR, "Could not allocate memory to parse config file");
		exit(1);
	} else if (result != 0) {
		wlr_log(L_ERROR, "Could not parse config file");
		exit(1);
	}

	return config;
}

void example_config_destroy(struct example_config *config) {
	struct output_config *oc, *otmp = NULL;
	wl_list_for_each_safe(oc, otmp, &config->outputs, link) {
		free(oc->name);
		free(oc);
	}

	struct device_config *dc, *dtmp = NULL;
	wl_list_for_each_safe(dc, dtmp, &config->devices, link) {
		free(dc->name);
		if (dc->mapped_output) {
			free(dc->mapped_output);
		}
		if (dc->mapped_box) {
			free(dc->mapped_box);
		}
		free(dc);
	}

	if (config->config_path) {
		free(config->config_path);
	}
	if (config->cursor.mapped_output) {
		free(config->cursor.mapped_output);
	}
	if (config->cursor.mapped_box) {
		free(config->cursor.mapped_box);
	}
	free(config);
}

struct output_config *example_config_get_output(struct example_config *config,
		struct wlr_output *output) {
	struct output_config *o_config;
	wl_list_for_each(o_config, &config->outputs, link) {
		if (strcmp(o_config->name, output->name) == 0) {
			return o_config;
		}
	}

	return NULL;
}

struct device_config *example_config_get_device(struct example_config *config,
		struct wlr_input_device *device) {
	struct device_config *d_config;
	wl_list_for_each(d_config, &config->devices, link) {
		if (strcmp(d_config->name, device->name) == 0) {
			return d_config;
		}
	}

	return NULL;
}

/**
 * Set device output mappings to NULL and configure region mappings.
 */
static void reset_device_mappings(struct example_config *config,
		struct wlr_cursor *cursor, struct wlr_input_device *device) {
	struct device_config *d_config;
	wlr_cursor_map_input_to_output(cursor, device, NULL);
	d_config = example_config_get_device(config, device);
	if (d_config) {
		wlr_cursor_map_input_to_region(cursor, device,
			d_config->mapped_box);
	}
}

static void set_device_output_mappings(struct example_config *config,
		struct wlr_cursor *cursor, struct wlr_output *output,
		struct wlr_input_device *device) {
	struct device_config *d_config;
	d_config = example_config_get_device(config, device);
	if (d_config &&
			d_config->mapped_output &&
			strcmp(d_config->mapped_output, output->name) == 0) {
		wlr_cursor_map_input_to_output(cursor, device, output);
	}
}

void example_config_configure_cursor(struct example_config *config,
		struct wlr_cursor *cursor, struct compositor_state *compositor) {
	struct pointer_state *p_state;
	struct tablet_tool_state *tt_state;
	struct touch_state *tch_state;
	struct output_state *o_state;

	// reset mappings
	wlr_cursor_map_to_output(cursor, NULL);
	wl_list_for_each(p_state, &compositor->pointers, link) {
		reset_device_mappings(config, cursor, p_state->device);
	}
	wl_list_for_each(tt_state, &compositor->tablet_tools, link) {
		reset_device_mappings(config, cursor, tt_state->device);
	}
	wl_list_for_each(tch_state, &compositor->touch, link) {
		reset_device_mappings(config, cursor, tch_state->device);
	}

	// configure device to output mappings
	char *mapped_output = config->cursor.mapped_output;
	wl_list_for_each(o_state, &compositor->outputs, link) {
		if (mapped_output && strcmp(mapped_output, o_state->output->name) == 0) {
			wlr_cursor_map_to_output(cursor, o_state->output);
		}

		wl_list_for_each(p_state, &compositor->pointers, link) {
			set_device_output_mappings(config, cursor, o_state->output,
				p_state->device);
		}
		wl_list_for_each(tt_state, &compositor->tablet_tools, link) {
			set_device_output_mappings(config, cursor, o_state->output,
				tt_state->device);
		}
		wl_list_for_each(tch_state, &compositor->touch, link) {
			set_device_output_mappings(config, cursor, o_state->output,
				tch_state->device);
		}
	}
}
