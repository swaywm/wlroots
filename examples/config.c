#include <stdlib.h>
#include <limits.h>
#include <getopt.h>
#include <string.h>
#include "shared.h"
#include "config.h"

static void usage(const char *name, int ret) {
	fprintf(stderr,
		"usage: %s [-d <name> [-r <rotation> | -f]]*\n"
		"\n"
		" -o <output>    The name of the DRM display. e.g. DVI-I-1.\n"
		" -r <rotation>  The rotation counter clockwise. Valid values are 90, 180, 270.\n"
		" -x <position>  The X-axis coordinate position of this output in the layout.\n"
		" -y <position>  The Y-axis coordinate position of this output in the layout.\n"
		" -f             Flip the output along the vertical axis.\n", name);

	exit(ret);
}

struct example_config *parse_args(int argc, char *argv[]) {
	struct example_config *config = calloc(1, sizeof(struct example_config));
	wl_list_init(&config->outputs);
	struct output_config *oc = NULL;

	int c;
	while ((c = getopt(argc, argv, "o:r:x:y:fh")) != -1) {
		switch (c) {
		case 'o':
			oc = calloc(1, sizeof(*oc));
			oc->name = optarg;
			oc->transform = WL_OUTPUT_TRANSFORM_NORMAL;
			wl_list_insert(&config->outputs, &oc->link);
			break;
		case 'r':
			if (!oc) {
				fprintf(stderr, "You must specify an output first\n");
				usage(argv[0], 1);
			}

			if (oc->transform != WL_OUTPUT_TRANSFORM_NORMAL
					&& oc->transform != WL_OUTPUT_TRANSFORM_FLIPPED) {
				fprintf(stderr, "Rotation for %s already specified\n", oc->name);
				usage(argv[0], 1);
			}

			if (strcmp(optarg, "90") == 0) {
				oc->transform += WL_OUTPUT_TRANSFORM_90;
			} else if (strcmp(optarg, "180") == 0) {
				oc->transform += WL_OUTPUT_TRANSFORM_180;
			} else if (strcmp(optarg, "270") == 0) {
				oc->transform += WL_OUTPUT_TRANSFORM_270;
			} else {
				fprintf(stderr, "Invalid rotation '%s'\n", optarg);
				usage(argv[0], 1);
			}
			break;
		case 'x':
			if (!oc) {
				fprintf(stderr, "You must specify an output first\n");
				usage(argv[0], 1);
			}
			oc->x = strtol(optarg, NULL, 0);
			break;
		case 'y':
			if (!oc) {
				fprintf(stderr, "You must specify an output first\n");
				usage(argv[0], 1);
			}
			oc->y = strtol(optarg, NULL, 0);
			break;
		case 'f':
			if (!oc) {
				fprintf(stderr, "You must specify an output first\n");
				usage(argv[0], 1);
			}

			if (oc->transform >= WL_OUTPUT_TRANSFORM_FLIPPED) {
				fprintf(stderr, "Flip for %s already specified\n", oc->name);
				usage(argv[0], 1);
			}

			oc->transform += WL_OUTPUT_TRANSFORM_FLIPPED;
			break;
		case 'h':
		case '?':
			usage(argv[0], c != 'h');
		}
	}

	return config;
}

void example_config_destroy(struct example_config *config) {
	struct output_config *oc, *tmp = NULL;
	wl_list_for_each_safe(oc, tmp, &config->outputs, link) {
		free(oc);
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

