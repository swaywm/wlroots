#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>
#include <time.h>
#include <GLES3/gl3.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdnoreturn.h>

#include "otd.h"
#include "drm.h"
#include "event.h"

// I know this is pretty crappy,
// but it's just for the example's sake
struct {
	char *name;
	char *mode;
} displays[8];
int num_displays = 0;

noreturn void usage(const char *name, int ret)
{
	fprintf(stderr, "usage: %s [-o <name> [-m <mode>]]*\n"
			"\n"
			"  -h       \tDisplay this help text\n"
			"  -o <name>\tWhich diplay to use. e.g. DVI-I-1.\n"
			"  -m <mode>\tWhich mode to use. It must come after an -o option.\n"
			"           \tMust be either 'preferred', 'current', widthxheight\n"
			"           \tor widthxheight@rate. Defaults to 'preferred'.\n"
			"\n"
			"example: %s -o DVI-I-1 -m 1920x1080@60 -o DP-1 -m 1920x1200\n",
			name, name);

	exit(ret);
}

void parse_args(int argc, char *argv[])
{
	int c;
	int i = -1;

	while ((c = getopt(argc, argv, "ho:m:")) != -1) {
		switch (c) {
		case 'h':
			usage(argv[0], 0);
		case 'o':
			i = num_displays++;

			if (num_displays == 8) {
				fprintf(stderr, "Too many displays\n");
				exit(1);
			}

			displays[i].name = strdup(optarg);
			break;
		case 'm':
			if (i == -1) {
				fprintf(stderr, "A display is required first\n");
				exit(1);
			}

			fprintf(stderr, "Assigned '%s' to '%s'\n", optarg, displays[i].name);
			displays[i].mode = optarg;
			i = -1;
			break;
		default:
			usage(argv[0], 1);
		}
	}

	// Trailing args
	if (optind != argc) {
		usage(argv[0], 1);
	}
}

int main(int argc, char *argv[])
{
	parse_args(argc, argv);

	struct otd *otd = otd_start();
	if (!otd)
		return 1;

	float colour[3] = {1.0, 0.0, 0.0};
	int dec = 0;

	struct timespec start, now;
	clock_gettime(CLOCK_MONOTONIC, &start);
	struct timespec last = start;

	while (clock_gettime(CLOCK_MONOTONIC, &now) == 0 &&
	       now.tv_sec < start.tv_sec + 10) {

		struct otd_event ev;
		if (!otd_get_event(otd, &ev))
			continue;

		struct otd_display *disp = ev.display;

		switch (ev.type) {
		case OTD_EV_RENDER:
			rendering_begin(disp);

			// This is all just calculating the colours.
			// It's not particularly important.

			long ms = (now.tv_sec - last.tv_sec) * 1000 +
				(now.tv_nsec - last.tv_nsec) / 1000000;

			int inc = (dec + 1) % 3;

			colour[dec] -= ms / 2000.0f;
			colour[inc] += ms / 2000.0f;

			if (colour[dec] < 0.0f) {
				colour[dec] = 0.0f;
				colour[inc] = 1.0f;

				dec = (dec + 1) % 3;
			}

			last = now;

			glViewport(0, 0, disp->width, disp->height);
			glClearColor(colour[0], colour[1], colour[2], 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);

			rendering_end(disp);
			break;
		case OTD_EV_DISPLAY_REM:
			printf("%s removed\n", disp->name);
			break;
		case OTD_EV_DISPLAY_ADD:
			printf("%s added\n", disp->name);

			const char *mode = NULL;
			for (int i = 0; i < num_displays; ++i) {
				if (strcmp(disp->name, displays[i].name) == 0) {
					mode = displays[i].mode;
				}
			}
			if (!mode)
				mode = "preferred";

			if (!modeset_str(otd, ev.display, mode)) {
				fprintf(stderr, "Modesetting %s failed\n", disp->name);
				goto out;
			}
			break;
		default:
			break;
		}
	}

out:
	otd_finish(otd);
}
