#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <assert.h>
#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/param.h>
#include <unistd.h>
#include <wlr/config.h>
#include <wlr/types/wlr_box.h>
#include <wlr/util/log.h>
#include "rootston/config.h"
#include "rootston/ini.h"
#include "rootston/input.h"
#include "rootston/keyboard.h"

static void usage(const char *name, int ret) {
	fprintf(stderr,
		"usage: %s [-C <FILE>] [-E <COMMAND>]\n"
		"\n"
		" -C <FILE>      Path to the configuration file\n"
		"                (default: rootston.ini).\n"
		"                See `rootston.ini.example` for config\n"
		"                file documentation.\n"
		" -E <COMMAND>   Command that will be ran at startup.\n"
		" -D             Enable damage tracking debugging.\n"
		" -l <LEVEL>     Set log verbosity, where,\n"
		"                0:SILENT, 1:ERROR, 2:INFO, 3+:DEBUG\n"
		"                (default: DEBUG)\n",
		name);

	exit(ret);
}

static struct wlr_box *parse_geometry(const char *str) {
	// format: {width}x{height}+{x}+{y}
	if (strlen(str) > 255) {
		wlr_log(WLR_ERROR, "cannot parse geometry string, too long");
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
	wlr_log(WLR_ERROR, "could not parse geometry string: %s", str);
	free(buf);
	free(box);
	return NULL;
}

static uint32_t parse_modifier(const char *symname) {
	if (strcmp(symname, "Shift") == 0) {
		return WLR_MODIFIER_SHIFT;
	} else if (strcmp(symname, "Caps") == 0) {
		return WLR_MODIFIER_CAPS;
	} else if (strcmp(symname, "Ctrl") == 0) {
		return WLR_MODIFIER_CTRL;
	} else if (strcmp(symname, "Alt") == 0) {
		return WLR_MODIFIER_ALT;
	} else if (strcmp(symname, "Mod2") == 0) {
		return WLR_MODIFIER_MOD2;
	} else if (strcmp(symname, "Mod3") == 0) {
		return WLR_MODIFIER_MOD3;
	} else if (strcmp(symname, "Logo") == 0) {
		return WLR_MODIFIER_LOGO;
	} else if (strcmp(symname, "Mod5") == 0) {
		return WLR_MODIFIER_MOD5;
	} else {
		return 0;
	}
}

static bool parse_modeline(const char *s, drmModeModeInfo *mode) {
	char hsync[16];
	char vsync[16];
	float fclock;

	mode->type = DRM_MODE_TYPE_USERDEF;

	if (sscanf(s, "%f %hd %hd %hd %hd %hd %hd %hd %hd %15s %15s",
		   &fclock,
		   &mode->hdisplay,
		   &mode->hsync_start,
		   &mode->hsync_end,
		   &mode->htotal,
		   &mode->vdisplay,
		   &mode->vsync_start,
		   &mode->vsync_end,
		   &mode->vtotal, hsync, vsync) != 11) {
		return false;
	}

	mode->clock = fclock * 1000;
	mode->vrefresh = mode->clock * 1000.0 * 1000.0
		/ mode->htotal / mode->vtotal;
	if (strcasecmp(hsync, "+hsync") == 0) {
		mode->flags |= DRM_MODE_FLAG_PHSYNC;
	} else if (strcasecmp(hsync, "-hsync") == 0) {
		mode->flags |= DRM_MODE_FLAG_NHSYNC;
	} else {
		return false;
	}

	if (strcasecmp(vsync, "+vsync") == 0) {
		mode->flags |= DRM_MODE_FLAG_PVSYNC;
	} else if (strcasecmp(vsync, "-vsync") == 0) {
		mode->flags |= DRM_MODE_FLAG_NVSYNC;
	} else {
		return false;
	}

	snprintf(mode->name, sizeof(mode->name), "%dx%d@%d",
		 mode->hdisplay, mode->vdisplay, mode->vrefresh / 1000);

	return true;
}

void add_binding_config(struct wl_list *bindings, const char* combination,
		const char* command) {
	struct roots_binding_config *bc =
		calloc(1, sizeof(struct roots_binding_config));

	xkb_keysym_t keysyms[ROOTS_KEYBOARD_PRESSED_KEYSYMS_CAP];
	char *symnames = strdup(combination);
	char *symname = strtok(symnames, "+");
	while (symname) {
		uint32_t modifier = parse_modifier(symname);
		if (modifier != 0) {
			bc->modifiers |= modifier;
		} else {
			xkb_keysym_t sym = xkb_keysym_from_name(symname,
				XKB_KEYSYM_NO_FLAGS);
			if (sym == XKB_KEY_NoSymbol) {
				wlr_log(WLR_ERROR, "got unknown key binding symbol: %s",
					symname);
				free(bc);
				bc = NULL;
				break;
			}
			keysyms[bc->keysyms_len] = sym;
			bc->keysyms_len++;
		}
		symname = strtok(NULL, "+");
	}
	free(symnames);

	if (bc) {
		wl_list_insert(bindings, &bc->link);
		bc->command = strdup(command);
		bc->keysyms = malloc(bc->keysyms_len * sizeof(xkb_keysym_t));
		memcpy(bc->keysyms, keysyms, bc->keysyms_len * sizeof(xkb_keysym_t));
	}
}

void add_switch_config(struct wl_list *switches, const char *switch_name, const char *action, 
		const char* command) {
	wlr_log(WLR_DEBUG, "config switch %s: %s = %s", switch_name, action, command);
	struct roots_switch_config *sc = calloc(1, sizeof(struct roots_switch_config));

	if (strcmp(switch_name, "tablet") == 0) {
		sc->switch_type = WLR_SWITCH_TYPE_TABLET_MODE;
	} else if (strcmp(switch_name, "lid") == 0) {
		sc->switch_type = WLR_SWITCH_TYPE_LID;
	} else {
		sc->switch_type = -1;
		sc->name = strdup(switch_name);
	}
	if (strcmp(action, "on") == 0) {
		sc->switch_state = WLR_SWITCH_STATE_ON;
	} else if (strcmp(action, "off") == 0) {
		sc->switch_state = WLR_SWITCH_STATE_OFF;
	} else if (strcmp(action, "toggle") == 0) {
		sc->switch_state = -1;
	} else {
		wlr_log(WLR_ERROR, "Invalid switch action %s/n for switch %s:%s",
		        action, switch_name, action);
	}
	sc->command = strdup(command);
	wl_list_insert(switches, &sc->link);
	wlr_log(WLR_DEBUG, "config %s: type %i state %i command %s", switch_name, sc->switch_type, sc->switch_state, sc->command);
}

static void config_handle_cursor(struct roots_config *config,
		const char *seat_name, const char *name, const char *value) {
	struct roots_cursor_config *cc;
	bool found = false;
	wl_list_for_each(cc, &config->cursors, link) {
		if (strcmp(cc->seat, seat_name) == 0) {
			found = true;
			break;
		}
	}

	if (!found) {
		cc = calloc(1, sizeof(struct roots_cursor_config));
		cc->seat = strdup(seat_name);
		wl_list_insert(&config->cursors, &cc->link);
	}

	if (strcmp(name, "map-to-output") == 0) {
		free(cc->mapped_output);
		cc->mapped_output = strdup(value);
	} else if (strcmp(name, "geometry") == 0) {
		free(cc->mapped_box);
		cc->mapped_box = parse_geometry(value);
	} else if (strcmp(name, "theme") == 0) {
		free(cc->theme);
		cc->theme = strdup(value);
	} else if (strcmp(name, "default-image") == 0) {
		free(cc->default_image);
		cc->default_image = strdup(value);
	} else {
		wlr_log(WLR_ERROR, "got unknown cursor config: %s", name);
	}
}

static void config_handle_keyboard(struct roots_config *config,
		const char *device_name, const char *name, const char *value) {
	struct roots_keyboard_config *kc;
	bool found = false;
	wl_list_for_each(kc, &config->keyboards, link) {
		if (strcmp(kc->name, device_name) == 0) {
			found = true;
			break;
		}
	}

	if (!found) {
		kc = calloc(1, sizeof(struct roots_keyboard_config));
		kc->name = strdup(device_name);
		wl_list_insert(&config->keyboards, &kc->link);
	}

	if (strcmp(name, "meta-key") == 0) {
		kc->meta_key = parse_modifier(value);
		if (kc->meta_key == 0) {
			wlr_log(WLR_ERROR, "got unknown meta key: %s", name);
		}
	} else if (strcmp(name, "rules") == 0) {
		kc->rules = strdup(value);
	} else if (strcmp(name, "model") == 0) {
		kc->model = strdup(value);
	} else if (strcmp(name, "layout") == 0) {
		kc->layout = strdup(value);
	} else if (strcmp(name, "variant") == 0) {
		kc->variant = strdup(value);
	} else if (strcmp(name, "options") == 0) {
		kc->options = strdup(value);
	} else if (strcmp(name, "repeat-rate") == 0) {
		kc->repeat_rate = strtol(value, NULL, 10);
	} else if (strcmp(name, "repeat-delay") == 0) {
		kc->repeat_delay = strtol(value, NULL, 10);
	} else {
		wlr_log(WLR_ERROR, "got unknown keyboard config: %s", name);
	}
}

static const char *output_prefix = "output:";
static const char *device_prefix = "device:";
static const char *keyboard_prefix = "keyboard:";
static const char *cursor_prefix = "cursor:";
static const char *switch_prefix = "switch:";

static int config_ini_handler(void *user, const char *section, const char *name,
		const char *value) {
	struct roots_config *config = user;
	if (strcmp(section, "core") == 0) {
		if (strcmp(name, "xwayland") == 0) {
			if (strcasecmp(value, "true") == 0) {
				config->xwayland = true;
			} else if (strcasecmp(value, "immediate") == 0) {
				config->xwayland = true;
				config->xwayland_lazy = false;
			} else if (strcasecmp(value, "false") == 0) {
				config->xwayland = false;
			} else {
				wlr_log(WLR_ERROR, "got unknown xwayland value: %s", value);
			}
		} else {
			wlr_log(WLR_ERROR, "got unknown core config: %s", name);
		}
	} else if (strncmp(output_prefix, section, strlen(output_prefix)) == 0) {
		const char *output_name = section + strlen(output_prefix);
		struct roots_output_config *oc;
		bool found = false;

		wl_list_for_each(oc, &config->outputs, link) {
			if (strcmp(oc->name, output_name) == 0) {
				found = true;
				break;
			}
		}

		if (!found) {
			oc = calloc(1, sizeof(struct roots_output_config));
			oc->name = strdup(output_name);
			oc->transform = WL_OUTPUT_TRANSFORM_NORMAL;
			oc->scale = 1;
			oc->enable = true;
			wl_list_init(&oc->modes);
			wl_list_insert(&config->outputs, &oc->link);
		}

		if (strcmp(name, "enable") == 0) {
			if (strcasecmp(value, "true") == 0) {
				oc->enable = true;
			} else if (strcasecmp(value, "false") == 0) {
				oc->enable = false;
			} else {
				wlr_log(WLR_ERROR, "got invalid output enable value: %s", value);
			}
		} else if (strcmp(name, "x") == 0) {
			oc->x = strtol(value, NULL, 10);
		} else if (strcmp(name, "y") == 0) {
			oc->y = strtol(value, NULL, 10);
		} else if (strcmp(name, "scale") == 0) {
			oc->scale = strtof(value, NULL);
			assert(oc->scale > 0);
		} else if (strcmp(name, "rotate") == 0) {
			if (strcmp(value, "normal") == 0) {
				oc->transform = WL_OUTPUT_TRANSFORM_NORMAL;
			} else if (strcmp(value, "90") == 0) {
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
				wlr_log(WLR_ERROR, "got unknown transform value: %s", value);
			}
		} else if (strcmp(name, "mode") == 0) {
			char *end;
			oc->mode.width = strtol(value, &end, 10);
			assert(*end == 'x');
			++end;
			oc->mode.height = strtol(end, &end, 10);
			if (*end) {
				assert(*end == '@');
				++end;
				oc->mode.refresh_rate = strtof(end, &end);
				assert(strcmp("Hz", end) == 0);
			}
			wlr_log(WLR_DEBUG, "Configured output %s with mode %dx%d@%f",
					oc->name, oc->mode.width, oc->mode.height,
					oc->mode.refresh_rate);
		} else if (strcmp(name, "modeline") == 0) {
			struct roots_output_mode_config *mode = calloc(1, sizeof(*mode));

			if (parse_modeline(value, &mode->info)) {
				wl_list_insert(&oc->modes, &mode->link);
			} else {
				free(mode);
				wlr_log(WLR_ERROR, "Invalid modeline: %s", value);
			}
		}
	} else if (strncmp(cursor_prefix, section, strlen(cursor_prefix)) == 0) {
		const char *seat_name = section + strlen(cursor_prefix);
		config_handle_cursor(config, seat_name, name, value);
	} else if (strcmp(section, "cursor") == 0) {
		config_handle_cursor(config, ROOTS_CONFIG_DEFAULT_SEAT_NAME, name,
			value);
	} else if (strncmp(device_prefix, section, strlen(device_prefix)) == 0) {
		const char *device_name = section + strlen(device_prefix);

		struct roots_device_config *dc;
		bool found = false;
		wl_list_for_each(dc, &config->devices, link) {
			if (strcmp(dc->name, device_name) == 0) {
				found = true;
				break;
			}
		}

		if (!found) {
			dc = calloc(1, sizeof(struct roots_device_config));
			dc->name = strdup(device_name);
			dc->seat = strdup(ROOTS_CONFIG_DEFAULT_SEAT_NAME);
			wl_list_insert(&config->devices, &dc->link);
		}

		if (strcmp(name, "map-to-output") == 0) {
			free(dc->mapped_output);
			dc->mapped_output = strdup(value);
		} else if (strcmp(name, "geometry") == 0) {
			free(dc->mapped_box);
			dc->mapped_box = parse_geometry(value);
		} else if (strcmp(name, "seat") == 0) {
			free(dc->seat);
			dc->seat = strdup(value);
		} else if (strcmp(name, "tap_enabled") == 0) {
			if (strcasecmp(value, "true") == 0) {
				dc->tap_enabled = true;
			} else if (strcasecmp(value, "false") == 0) {
				dc->tap_enabled = false;
			} else {
				wlr_log(WLR_ERROR,
					"got unknown tap_enabled value: %s",
					value);
			}
		} else {
			wlr_log(WLR_ERROR, "got unknown device config: %s", name);
		}
	} else if (strcmp(section, "keyboard") == 0) {
		config_handle_keyboard(config, "", name, value);
	} else if (strncmp(keyboard_prefix,
				section, strlen(keyboard_prefix)) == 0) {
		const char *device_name = section + strlen(keyboard_prefix);
		config_handle_keyboard(config, device_name, name, value);
	} else if (strcmp(section, "bindings") == 0) {
		add_binding_config(&config->bindings, name, value);
	} else if (strncmp(switch_prefix, section, strlen(switch_prefix)) == 0) {
		const char *switch_name = section + strlen(switch_prefix);
		add_switch_config(&config->bindings, switch_name, name, value);
	} else {
		wlr_log(WLR_ERROR, "got unknown config section: %s", section);
	}

	return 1;
}

struct roots_config *roots_config_create_from_args(int argc, char *argv[]) {
	struct roots_config *config = calloc(1, sizeof(struct roots_config));
	if (config == NULL) {
		return NULL;
	}

	config->xwayland = true;
	config->xwayland_lazy = true;
	wl_list_init(&config->outputs);
	wl_list_init(&config->devices);
	wl_list_init(&config->keyboards);
	wl_list_init(&config->cursors);
	wl_list_init(&config->bindings);
	wl_list_init(&config->switches);

	int c;
	unsigned int log_verbosity = WLR_DEBUG;
	while ((c = getopt(argc, argv, "C:E:hDl:")) != -1) {
		switch (c) {
		case 'C':
			config->config_path = strdup(optarg);
			break;
		case 'E':
			config->startup_cmd = strdup(optarg);
			break;
		case 'D':
			config->debug_damage_tracking = true;
			break;
		case 'l':
			log_verbosity = strtoul(optarg, NULL, 10);
			if (log_verbosity >= WLR_LOG_IMPORTANCE_LAST) {
				log_verbosity = WLR_LOG_IMPORTANCE_LAST - 1;
			}
			break;
		case 'h':
		case '?':
			usage(argv[0], c != 'h');
		}
	}
	wlr_log_init(log_verbosity, NULL);

	if (!config->config_path) {
		// get the config path from the current directory
		char cwd[MAXPATHLEN];
		if (getcwd(cwd, sizeof(cwd)) != NULL) {
			char buf[MAXPATHLEN];
			if (snprintf(buf, MAXPATHLEN, "%s/%s", cwd, "rootston.ini") >= MAXPATHLEN) {
				wlr_log(WLR_ERROR, "config path too long");
				exit(1);
			}
			config->config_path = strdup(buf);
		} else {
			wlr_log(WLR_ERROR, "could not get cwd");
			exit(1);
		}
	}

	int result = ini_parse(config->config_path, config_ini_handler, config);

	if (result == -1) {
		wlr_log(WLR_DEBUG, "No config file found. Using sensible defaults.");
		add_binding_config(&config->bindings, "Logo+Shift+E", "exit");
		add_binding_config(&config->bindings, "Ctrl+q", "close");
		add_binding_config(&config->bindings, "Alt+Tab", "next_window");
		add_binding_config(&config->bindings, "Logo+Escape", "break_pointer_constraint");
		struct roots_keyboard_config *kc =
			calloc(1, sizeof(struct roots_keyboard_config));
		kc->meta_key = WLR_MODIFIER_LOGO;
		kc->name = strdup("");
		wl_list_insert(&config->keyboards, &kc->link);
	} else if (result == -2) {
		wlr_log(WLR_ERROR, "Could not allocate memory to parse config file");
		exit(1);
	} else if (result != 0) {
		wlr_log(WLR_ERROR, "Could not parse config file");
		exit(1);
	}

	return config;
}

void roots_config_destroy(struct roots_config *config) {
	struct roots_output_config *oc, *otmp = NULL;
	wl_list_for_each_safe(oc, otmp, &config->outputs, link) {
		struct roots_output_mode_config *omc, *omctmp = NULL;
		wl_list_for_each_safe(omc, omctmp, &oc->modes, link) {
			free(omc);
		}
		free(oc->name);
		free(oc);
	}

	struct roots_device_config *dc, *dtmp = NULL;
	wl_list_for_each_safe(dc, dtmp, &config->devices, link) {
		free(dc->name);
		free(dc->seat);
		free(dc->mapped_output);
		free(dc->mapped_box);
		free(dc);
	}

	struct roots_keyboard_config *kc, *ktmp = NULL;
	wl_list_for_each_safe(kc, ktmp, &config->keyboards, link) {
		free(kc->name);
		free(kc->rules);
		free(kc->model);
		free(kc->layout);
		free(kc->variant);
		free(kc->options);
		free(kc);
	}

	struct roots_cursor_config *cc, *ctmp = NULL;
	wl_list_for_each_safe(cc, ctmp, &config->cursors, link) {
		free(cc->seat);
		free(cc->mapped_output);
		free(cc->mapped_box);
		free(cc->theme);
		free(cc->default_image);
		free(cc);
	}

	struct roots_binding_config *bc, *btmp = NULL;
	wl_list_for_each_safe(bc, btmp, &config->bindings, link) {
		free(bc->keysyms);
		free(bc->command);
		free(bc);
	}

	free(config->config_path);
	free(config);
}

struct roots_output_config *roots_config_get_output(struct roots_config *config,
		struct wlr_output *output) {
	char name[88];
	snprintf(name, sizeof(name), "%s %s %s", output->make, output->model,
		output->serial);

	struct roots_output_config *oc;
	wl_list_for_each(oc, &config->outputs, link) {
		if (strcmp(oc->name, output->name) == 0 ||
				strcmp(oc->name, name) == 0) {
			return oc;
		}
	}

	return NULL;
}

struct roots_device_config *roots_config_get_device(struct roots_config *config,
		struct wlr_input_device *device) {
	struct roots_device_config *d_config;
	wl_list_for_each(d_config, &config->devices, link) {
		if (strcmp(d_config->name, device->name) == 0) {
			return d_config;
		}
	}

	return NULL;
}

struct roots_keyboard_config *roots_config_get_keyboard(
		struct roots_config *config, struct wlr_input_device *device) {
	const char *device_name = "";
	if (device != NULL) {
		device_name = device->name;
	}

	struct roots_keyboard_config *kc;
	wl_list_for_each(kc, &config->keyboards, link) {
		if (strcmp(kc->name, device_name) == 0) {
			return kc;
		}
	}

	return NULL;
}

struct roots_cursor_config *roots_config_get_cursor(struct roots_config *config,
		const char *seat_name) {
	if (seat_name == NULL) {
		seat_name = ROOTS_CONFIG_DEFAULT_SEAT_NAME;
	}

	struct roots_cursor_config *cc;
	wl_list_for_each(cc, &config->cursors, link) {
		if (strcmp(cc->seat, seat_name) == 0) {
			return cc;
		}
	}

	return NULL;
}
