#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdlib.h>
#include <limits.h>
#include <getopt.h>
#include <string.h>
#include <unistd.h>
#include <wlr/util/log.h>
#include "shared.h"
#include "config.h"
#include "ini.h"

static void usage(const char *name, int ret) {
	fprintf(stderr,
		"usage: %s [-C <FILE>]\n"
		"\n"
		" -C <FILE>      Path to the configuration file (default: wlr-example.ini).\n"
		"                See `examples/wlr-example.ini.example` for config file documentation.\n", name);

	exit(ret);
}

static const char *output_prefix = "output:";

static int config_ini_handler(void *user, const char *section, const char *name, const char *value) {
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
	} else {
		wlr_log(L_ERROR, "got unknown config section: %s", section);
	}

	return 1;
}

struct example_config *parse_args(int argc, char *argv[]) {
	struct example_config *config = calloc(1, sizeof(struct example_config));
	wl_list_init(&config->outputs);

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
		char cwd[1024];
		if (getcwd(cwd, sizeof(cwd)) != NULL) {
			char buf[1024];
			sprintf(buf, "%s/%s", cwd, "wlr-example.ini");
			config->config_path = strdup(buf);
		} else {
			wlr_log(L_ERROR, "could not get cwd");
			exit(1);
		}
	}

	int result = ini_parse(config->config_path, config_ini_handler, config);

	if (result == -1) {
		wlr_log(L_ERROR, "Could not find config file at %s", config->config_path);
		exit(1);
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
	struct output_config *oc, *tmp = NULL;
	wl_list_for_each_safe(oc, tmp, &config->outputs, link) {
		free(oc->name);
		free(oc);
	}
	if (config->config_path) {
		free(config->config_path);
	}
	free(config);
}

struct wlr_output_layout *configure_layout(struct example_config *config, struct wl_list *outputs) {
	struct wlr_output_layout *layout = wlr_output_layout_init();
	int max_x = INT_MIN;
	int max_x_y = INT_MIN; // y value for the max_x output

	// first add all the configured outputs
	struct output_state *output;
	wl_list_for_each(output, outputs, link) {
		struct output_config *conf;
		wl_list_for_each(conf, &config->outputs, link) {
			if (strcmp(conf->name, output->output->name) == 0) {
				wlr_output_layout_add(layout, output->output,
						conf->x, conf->y);
				wlr_output_transform(output->output, conf->transform);
				int width, height;
				wlr_output_effective_resolution(output->output, &width, &height);
				if (conf->x + width > max_x) {
					max_x = conf->x + width;
					max_x_y = conf->y;
				}
				break;
			}
		}
	}

	if (max_x == INT_MIN) {
		// couldn't find a configured output
		max_x = 0;
		max_x_y = 0;
	}

	// now add all the other configured outputs in a sensible position
	wl_list_for_each(output, outputs, link) {
		if (wlr_output_layout_get(layout, output->output)) {
			continue;
		}
		wlr_output_layout_add(layout, output->output, max_x, max_x_y);
		int width, height;
		wlr_output_effective_resolution(output->output, &width, &height);
		max_x += width;
	}

	return layout;
}

